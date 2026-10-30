#include "routine.hpp"
#include "program.hpp"
#include "game/actions/hive/hive_setup.hpp"
#include <limits>
#include "runtime.hpp"
#include "camera_options.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdlib>
#include <cstring>
#include <set>
#include <algorithm>

namespace hive::scripting {
using namespace automation;
using namespace routine;
namespace {
using detail::Memory;
using detail::memory_limit;
using detail::Vm;
using detail::budget;
void raw_field(lua_State* state,int index,const char* name) { index=lua_absindex(state,index); lua_pushstring(state,name); lua_rawget(state,index); }
std::string string_field(lua_State* state,int index,const char* name) {
    raw_field(state,index,name);
    if (lua_type(state,-1)!=LUA_TSTRING) throw std::runtime_error(std::string("Expected text: ")+name);
    std::size_t size{}; const auto* data=lua_tolstring(state,-1,&size);
    if (size>160 || std::memchr(data,0,size)) throw std::runtime_error("Invalid script text");
    std::string result(data,size); lua_pop(state,1); return result;
}
double number_field(lua_State* state,int index,const char* name,double low,double high) {
    raw_field(state,index,name);
    if (lua_type(state,-1)!=LUA_TNUMBER) throw std::runtime_error(std::string("Expected number: ")+name);
    const auto result=lua_tonumber(state,-1); lua_pop(state,1);
    if (!std::isfinite(result) || result<low || result>high) throw std::runtime_error(std::string("Value outside limits: ")+name);
    return result;
}
lua_Integer integer_field(lua_State* state,int index,const char* name,lua_Integer minimum) {
    raw_field(state,index,name);
    if (!lua_isinteger(state,-1)) throw std::runtime_error(std::string("Expected integer: ")+name);
    const auto value=lua_tointeger(state,-1); lua_pop(state,1);
    if (value<minimum) throw std::runtime_error(std::string("Integer outside limits: ")+name);
    return value;
}
std::size_t dense_list(lua_State* state,int index,std::size_t maximum) {
    index=lua_absindex(state,index);
    if (!lua_istable(state,index) || lua_rawlen(state,index)>maximum) throw std::runtime_error("Request list exceeds limits");
    const auto size=lua_rawlen(state,index);
    lua_pushnil(state);
    while (lua_next(state,index)) {
        if (!lua_isinteger(state,-2) || lua_tointeger(state,-2)<1 || static_cast<std::size_t>(lua_tointeger(state,-2))>size)
            throw std::runtime_error("Requests need dense lists");
        lua_pop(state,1);
    }
    return size;
}
std::string request_text(lua_State* state,int index,std::size_t limit=160) {
    if (lua_type(state,index)!=LUA_TSTRING) throw std::runtime_error("Request value must be text");
    std::size_t size{}; const auto* data=lua_tolstring(state,index,&size);
    if (size>limit || std::memchr(data,0,size)) throw std::runtime_error("Request text exceeds limits");
    return {data,size};
}
Settings request_data(lua_State* state,int index) {
    index=lua_absindex(state,index);
    if (!lua_istable(state,index)) throw std::runtime_error("Request data must be a table");
    Settings data;
    lua_pushnil(state);
    while (lua_next(state,index)) {
        const auto key=request_text(state,-2,80);
        if (data.size()>=16) throw std::runtime_error("Too many request fields");
        SettingValue value;
        if (lua_isboolean(state,-1)) value=bool(lua_toboolean(state,-1));
        else if (lua_isinteger(state,-1)) {
            const auto number=lua_tointeger(state,-1);
            if (number<std::numeric_limits<int>::min() || number>std::numeric_limits<int>::max()) throw std::runtime_error("Request integer exceeds limits");
            value=static_cast<int>(number);
        } else if (lua_type(state,-1)==LUA_TSTRING) value=request_text(state,-1);
        else if (lua_istable(state,-1)) {
            SettingSelection items;
            const auto size=dense_list(state,-1,64);
            for (std::size_t i=1;i<=size;++i) {
                lua_rawgeti(state,-1,static_cast<lua_Integer>(i)); items.push_back(request_text(state,-1)); lua_pop(state,1);
            }
            value=std::move(items);
        } else throw std::runtime_error("Unsupported request value");
        data.emplace(key,std::move(value)); lua_pop(state,1);
    }
    return data;
}
void push_request_data(lua_State* state,const Settings& data) {
    lua_newtable(state);
    for (const auto& [key,value] : data) {
        std::visit([&](const auto& v) {
            using T=std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T,bool>) lua_pushboolean(state,v);
            else if constexpr (std::is_same_v<T,int>) lua_pushinteger(state,v);
            else if constexpr (std::is_same_v<T,std::string>) lua_pushlstring(state,v.data(),v.size());
            else {
                lua_newtable(state); lua_Integer i=0;
                for (const auto& item : v) { lua_pushlstring(state,item.data(),item.size()); lua_rawseti(state,-2,++i); }
            }
        },value);
        lua_setfield(state,-2,key.c_str());
    }
}
void push_requests(lua_State* state,const game::RequestInbox& inbox) {
    lua_newtable(state);
    for (const auto& [channel,messages] : inbox) {
        lua_newtable(state); lua_Integer i=0;
        for (const auto& message : messages) {
            lua_newtable(state);
            lua_pushlstring(state,message.owner.data(),message.owner.size()); lua_setfield(state,-2,"owner");
            lua_pushlstring(state,message.id.data(),message.id.size()); lua_setfield(state,-2,"id");
            lua_pushinteger(state,message.priority); lua_setfield(state,-2,"priority");
            lua_pushinteger(state,message.expires.count()); lua_setfield(state,-2,"expires_ms");
            lua_pushinteger(state,static_cast<lua_Integer>(message.generation)); lua_setfield(state,-2,"generation");
            push_request_data(state,message.data); lua_setfield(state,-2,"data");
            lua_rawseti(state,-2,++i);
        }
        lua_setfield(state,-2,channel.c_str());
    }
}
game::Requests request_batch(lua_State* state) {
    game::Requests result;
    result.generation=static_cast<std::uint64_t>(integer_field(state,-1,"generation",1));
    result.revision=static_cast<std::uint64_t>(integer_field(state,-1,"revision",1));
    raw_field(state,-1,"fields");
    if (!lua_isnil(state,-1)) {
        const auto count=dense_list(state,-1,32);
        for (std::size_t i=1;i<=count;++i) {
            lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
            if (!lua_istable(state,-1)) throw std::runtime_error("Invalid field request");
            const auto priority=integer_field(state,-1,"priority",0);
            if (priority>100) throw std::runtime_error("Field priority exceeds limits");
            result.fields.push_back({string_field(state,-1,"id"),string_field(state,-1,"field"),static_cast<int>(priority),
                Time(integer_field(state,-1,"expires_ms",0))});
            raw_field(state,-1,"alternatives");
            if (!lua_isnil(state,-1)) {
                const auto size=dense_list(state,-1,63);
                for (std::size_t j=1;j<=size;++j) {
                    lua_rawgeti(state,-1,static_cast<lua_Integer>(j));
                    if (lua_type(state,-1)!=LUA_TSTRING) throw std::runtime_error("Field alternative must be text");
                    std::size_t length{}; const auto value=lua_tolstring(state,-1,&length);
                    if (length>80 || std::memchr(value,0,length)) throw std::runtime_error("Invalid field alternative");
                    result.fields.back().alternatives.emplace_back(value,length); lua_pop(state,1);
                }
            }
            lua_pop(state,1);
            game::validate_field_targets(result.fields.back().field,result.fields.back().alternatives);
            lua_pop(state,1);
        }
    }
    lua_pop(state,1);
    raw_field(state,-1,"enemies");
    if (!lua_isnil(state,-1)) {
        const auto size=dense_list(state,-1,32);
        for (std::size_t i=1;i<=size;++i) {
            lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
            if (!lua_istable(state,-1)) throw std::runtime_error("Invalid enemy request");
            const auto priority=integer_field(state,-1,"priority",0);
            if (priority>100) throw std::runtime_error("Enemy priority exceeds limits");
            result.enemies.push_back({string_field(state,-1,"id"),string_field(state,-1,"enemy"),static_cast<int>(priority),
                Time(integer_field(state,-1,"expires_ms",0))});
            lua_pop(state,1);
        }
    }
    lua_pop(state,1);
    raw_field(state,-1,"channels");
    if (!lua_isnil(state,-1)) {
        if (!lua_istable(state,-1)) throw std::runtime_error("Request channels must be a table");
        lua_pushnil(state);
        while (lua_next(state,-2)) {
            const auto channel=request_text(state,-2,80);
            if (result.channels.size()>=64) throw std::runtime_error("Too many request channels");
            const auto size=dense_list(state,-1,32);
            auto& requests=result.channels[channel];
            for (std::size_t i=1;i<=size;++i) {
                lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
                if (!lua_istable(state,-1)) throw std::runtime_error("Invalid channel request");
                const auto priority=integer_field(state,-1,"priority",0);
                if (priority>100) throw std::runtime_error("Request priority exceeds limits");
                game::ChannelRequest request{string_field(state,-1,"id"),static_cast<int>(priority),Time(integer_field(state,-1,"expires_ms",0)),{}};
                raw_field(state,-1,"data"); request.data=request_data(state,-1); lua_pop(state,1);
                requests.push_back(std::move(request)); lua_pop(state,1);
            }
            lua_pop(state,1);
        }
    }
    lua_pop(state,1); return result;
}
bool boolean_field(lua_State* state,int index,const char* name) {
    raw_field(state,index,name);
    if (!lua_isnil(state,-1) && !lua_isboolean(state,-1)) throw std::runtime_error(std::string("Expected boolean: ")+name);
    const bool value=lua_toboolean(state,-1); lua_pop(state,1); return value;
}
Key key(const std::string& value) {
    if (value=="forward") return Key::forward;
    if (value=="backward") return Key::backward;
    if (value=="left") return Key::left;
    if (value=="right") return Key::right;
    if (value=="rotate-left") return Key::rotate_left;
    if (value=="rotate-right") return Key::rotate_right;
    if (value=="pitch-up") return Key::pitch_up;
    if (value=="pitch-down") return Key::pitch_down;
    if (value=="zoom-in") return Key::zoom_in;
    if (value=="zoom-out") return Key::zoom_out;
    if (value=="jump") return Key::jump;
    if (value=="primary") return Key::primary;
    throw std::runtime_error("Unsupported pattern key: "+value);
}
bool has_field(lua_State* state,int index,const char* name) {
    raw_field(state,index,name); const bool result=!lua_isnil(state,-1); lua_pop(state,1); return result;
}
Walk walk_field(lua_State* state,int index,double speed) {
    index=lua_absindex(state,index);
    Walk result{{number_field(state,index,"tiles",0.001,1000),key(string_field(state,index,"direction")),{}},boolean_field(state,index,"tool")};
    if (has_field(state,index,"second")) result.leg.second=key(string_field(state,index,"second"));
    if (has_field(state,index,"settle_ms")) {
        const auto settle=number_field(state,index,"settle_ms",0,250);
        if (settle!=std::floor(settle)) throw std::runtime_error("Walk settling time must be whole milliseconds");
        result.settle=Time(static_cast<Time::rep>(settle));
    }
    game::movement_slice(result.leg,result.leg.tiles,speed); return result;
}
game::TextTarget target_field(lua_State* state,int index,const char* name,Rect region={.2,0,.6,.3}) {
    raw_field(state,index,name); if (!lua_istable(state,-1)) throw std::runtime_error("Expected text target table");
    const auto table=lua_absindex(state,-1);
    game::TextTarget result{string_field(state,table,"words"),region};
    if (has_field(state,table,"x")) result.region={number_field(state,table,"x",0,1),number_field(state,table,"y",0,1),number_field(state,table,"width",0.001,1),number_field(state,table,"height",0.001,1)};
    result.cooldown=boolean_field(state,table,"cooldown"); game::validate(result); lua_pop(state,1); return result;
}
std::vector<Action> checkpoint_actions(lua_State* state,int index,const char* name) {
    raw_field(state,index,name);
    if (!lua_istable(state,-1) || lua_rawlen(state,-1)>256) throw std::runtime_error("Checkpoint needs at most 256 movement actions");
    std::vector<Action> actions;
    const auto count=lua_rawlen(state,-1);
    for (std::size_t i=1;i<=count;++i) {
        lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
        if (!lua_istable(state,-1)) throw std::runtime_error("Invalid checkpoint action");
        Action action{name,{key(string_field(state,-1,"key"))},
            Time(static_cast<Time::rep>(number_field(state,-1,"milliseconds",1,250))),{}};
        if (has_field(state,-1,"second")) action.held.push_back(key(string_field(state,-1,"second")));
        actions.push_back(std::move(action)); lua_pop(state,1);
    }
    lua_pop(state,1); return actions;
}
std::vector<game::WalkLeg> checkpoint_path(lua_State* state,int index,const char* name,double speed) {
    raw_field(state,index,name);
    if (!lua_istable(state,-1) || lua_rawlen(state,-1)>64) throw std::runtime_error("Checkpoint needs at most 64 walk legs");
    const auto count=lua_rawlen(state,-1);
    lua_pushnil(state);
    while (lua_next(state,-2)) {
        if (!lua_isinteger(state,-2) || lua_tointeger(state,-2)<1 || static_cast<std::size_t>(lua_tointeger(state,-2))>count)
            throw std::runtime_error("Checkpoint path must be a dense list");
        lua_pop(state,1);
    }
    std::vector<game::WalkLeg> result;
    for (std::size_t i=1;i<=count;++i) {
        lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
        if (!lua_istable(state,-1)) throw std::runtime_error("Invalid checkpoint walk leg");
        const auto walk=walk_field(state,-1,speed);
        if (walk.tool || walk.settle>Time{}) throw std::runtime_error("Checkpoint walk cannot use the tool or a settling interval");
        result.push_back(walk.leg); lua_pop(state,1);
    }
    lua_pop(state,1); return result;
}
ApproachPrompt approach_prompt(lua_State* state,double speed) {
    const auto table=lua_absindex(state,-1);
    const auto target=target_field(state,table,"target");
    if (target.cooldown) throw std::runtime_error("Prompt approach cannot target a cooldown");
    ApproachPrompt result;
    result.prompt={{target.words},target.region}; result.movement.detector=target.words;
    if (has_field(state,table,"confidence")) result.prompt.confidence=static_cast<float>(number_field(state,table,"confidence",0,1));
    result.movement.speed=speed;
    const bool path=has_field(state,table,"path"),approach=has_field(state,table,"approach");
    if (path==approach) throw std::runtime_error("Checkpoint requires either approach or path");
    if (path) result.movement.path=checkpoint_path(state,table,"path",speed);
    else result.movement.approach=checkpoint_actions(state,table,"approach");
    if (has_field(state,table,"nudge_path")) {
        if (has_field(state,table,"nudge")) throw std::runtime_error("Checkpoint requires one nudge format");
        result.movement.nudge_path=checkpoint_path(state,table,"nudge_path",speed);
    }
    if (has_field(state,table,"nudge")) result.movement.nudge=checkpoint_actions(state,table,"nudge");
    result.movement.timeout=Time(static_cast<Time::rep>(number_field(state,table,"timeout_ms",100,120000)));
    if (has_field(state,table,"attempts")) {
        const auto attempts=number_field(state,table,"attempts",0,16);
        if (attempts!=std::floor(attempts)) throw std::runtime_error("Checkpoint attempts must be a whole number");
        result.movement.attempts=static_cast<std::size_t>(attempts);
    }
    game::validate(result.prompt); workflows::validate(result.movement); return result;
}
Step step_value(lua_State* state,double speed) {
    if (!lua_istable(state,-1)) throw std::runtime_error("Expected operation table");
    const auto kind=string_field(state,-1,"kind");
    if (kind=="done") return Done{string_field(state,-1,"reason")};
    if (kind=="defer") return Defer{string_field(state,-1,"reason")};
    if (kind=="cycle_end") return EndCycle{};
    if (kind=="walk") return walk_field(state,-1,speed);
    if (kind=="camera") {
        const auto steps=number_field(state,-1,"steps",1,32);
        if (steps!=std::floor(steps)) throw std::runtime_error("Camera steps must be a whole number");
        MoveCamera result{{key(string_field(state,-1,"key")),static_cast<std::size_t>(steps)}};
        detail::camera_options(state,-1,result.movement);
        return result;
    }
    if (kind=="convert") return Convert{false,has_field(state,-1,"after_gather") && boolean_field(state,-1,"after_gather")};
    if (kind=="navigate") return Navigate{string_field(state,-1,"destination")};
    if (kind=="approach_prompt") return approach_prompt(state,speed);
    if (kind=="approach_hive") return hive_row_approach(speed);
    if (kind=="answer_dialog") {
        const auto question=target_field(state,-1,"question",{.1,.15,.8,.7});
        const auto choice=string_field(state,-1,"choice");
        if (question.cooldown || (choice!="yes" && choice!="no")) throw std::runtime_error("Dialog needs a question and a yes/no choice");
        workflows::DialogPlan plan{{{question.words},question.region},choice=="yes"?workflows::DialogChoice::yes:workflows::DialogChoice::no,
            Time(static_cast<Time::rep>(number_field(state,-1,"timeout_ms",1000,30000)))};
        workflows::validate(plan); return AnswerDialog{std::move(plan)};
    }
    if (kind=="station") return UseStation{string_field(state,-1,"name"),Time(static_cast<Time::rep>(number_field(state,-1,"timeout_ms",1000,60000)))};
    if (kind=="await_text") return AwaitText{target_field(state,-1,"target"),Time(static_cast<Time::rep>(number_field(state,-1,"timeout_ms",100,60000)))};
    if (kind=="pattern") {
        Pattern result; result.duration=Time(static_cast<Time::rep>(number_field(state,-1,"duration_ms",1000,28800000)));
        result.label=string_field(state,-1,"label");
        if (has_field(state,-1,"backpack_limit")) result.backpack_limit=number_field(state,-1,"backpack_limit",1,100);
        if (has_field(state,-1,"until_text")) result.until=target_field(state,-1,"until_text");
        raw_field(state,-1,"legs");
        if (!lua_istable(state,-1) || lua_rawlen(state,-1)<1 || lua_rawlen(state,-1)>128) throw std::runtime_error("Pattern needs 1–128 legs");
        const auto count=lua_rawlen(state,-1);
        for (std::size_t i=1;i<=count;++i) {
            lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
            if (!lua_istable(state,-1)) throw std::runtime_error("Invalid pattern leg");
            result.legs.push_back(walk_field(state,-1,speed)); lua_pop(state,1);
        }
        lua_pop(state,1); return result;
    }
    if (kind=="wait" || kind=="press") {
        Hold result{{},Time(static_cast<Time::rep>(number_field(state,-1,"milliseconds",1,10000)))};
        if (kind=="press") result.keys.push_back(key(string_field(state,-1,"key")));
        return result;
    }
    throw std::runtime_error("Unknown feature operation: "+kind);
}
std::vector<Step> sequence_field(lua_State* state,const char* name,double speed,std::size_t limit=256) {
    raw_field(state,-1,name);
    if (!lua_istable(state,-1) || lua_rawlen(state,-1)<1 || lua_rawlen(state,-1)>limit) throw std::runtime_error("Routine exceeds its operation limit");
    std::vector<Step> steps;
    const auto count=lua_rawlen(state,-1);
    for (std::size_t i=1;i<=count;++i) {
        lua_rawgeti(state,-1,static_cast<lua_Integer>(i)); steps.push_back(step_value(state,speed)); lua_pop(state,1);
    }
    lua_pop(state,1); return steps;
}
Routine routine_field(lua_State* state,RunConfig config) {
    raw_field(state,-1,"routine");
    if (!lua_istable(state,-1)) throw std::runtime_error("Expected routine table");
    Routine result; result.config=config;
    result.anchor={target_field(state,-1,"anchor"),Time(10000)};
    result.gather=sequence_field(state,"gather",config.speed);
    auto remaining=256-result.gather.size();
    raw_field(state,-1,"activities");
    if (!lua_istable(state,-1) || lua_rawlen(state,-1)>32) throw std::runtime_error("Routine supports at most 32 activities");
    const auto count=lua_rawlen(state,-1);
    for (std::size_t i=1;i<=count;++i) {
        lua_rawgeti(state,-1,static_cast<lua_Integer>(i));
        if (!lua_istable(state,-1)) throw std::runtime_error("Expected activity table");
        Activity activity; activity.id=string_field(state,-1,"id");
        activity.priority=static_cast<int>(number_field(state,-1,"priority",0,100));
        activity.cooldown=Time(static_cast<Time::rep>(number_field(state,-1,"cooldown_ms",1000,86400000)));
        if (has_field(state,-1,"boost_ms")) activity.boost_duration=Time(static_cast<Time::rep>(number_field(state,-1,"boost_ms",0,14400000)));
        activity.during_boost=boolean_field(state,-1,"during_boost");
        const auto confirmation=number_field(state,-1,"confirmation",1,256);
        if (confirmation!=std::floor(confirmation)) throw std::runtime_error("Confirmation must be a sequence index");
        activity.confirmation=static_cast<std::size_t>(confirmation)-1;
        activity.steps=sequence_field(state,"steps",config.speed,remaining);
        remaining-=activity.steps.size();
        result.activities.push_back(std::move(activity)); lua_pop(state,1);
    }
    lua_pop(state,2); return result;
}
enum class ScriptMode { program, contributor, pattern };
class Script final : public Program,public Contributor {
    Vm vm_;
    Sources sources_;
    Settings settings_;
    extensions::CommandArguments arguments_;
    RunConfig config_;
    std::unique_ptr<Program> routine_;
    View view_;
    ContributorView contributor_view_;
    ScriptMode mode_;
    std::vector<std::string> buffs_;
    int module_{LUA_NOREF};
    static int initialize(lua_State* state) {
        auto& self=*static_cast<Script*>(lua_touserdata(state,1));
        luaL_requiref(state,"_G",luaopen_base,1); lua_pop(state,1);
        for (auto name : {"dofile","loadfile","load","collectgarbage","print","warn","pcall","xpcall","setmetatable","getmetatable"}) {
            lua_pushnil(state); lua_setglobal(state,name);
        }
        luaL_requiref(state,"math",luaopen_math,1); lua_pop(state,1);
        lua_newtable(state);
        for (const auto& [name,source] : self.sources_) {
            if (name=="main") continue;
            if (luaL_loadbufferx(state,source.data(),source.size(),name.c_str(),"t")!=LUA_OK) return lua_error(state);
            lua_call(state,0,1); lua_setfield(state,-2,name.c_str());
        }
        lua_setglobal(state,"patterns");
        const auto& source=self.sources_.at("main");
        if (luaL_loadbufferx(state,source.data(),source.size(),"main","t")!=LUA_OK) return lua_error(state);
        lua_call(state,0,1);
        if (!lua_istable(state,-1)) return luaL_error(state,"Feature must return a table");
        self.module_=luaL_ref(state,LUA_REGISTRYINDEX);
        lua_rawgeti(state,LUA_REGISTRYINDEX,self.module_); raw_field(state,-1,"configure");
        if (!lua_isfunction(state,-1)) return luaL_error(state,"Missing configure function");
        lua_newtable(state);
        for (const auto& [name,value] : self.settings_) {
            if (const auto* b=std::get_if<bool>(&value)) lua_pushboolean(state,*b);
            else if (const auto* n=std::get_if<int>(&value)) lua_pushinteger(state,*n);
            else if (const auto* text=std::get_if<std::string>(&value)) lua_pushlstring(state,text->data(),text->size());
            else {
                lua_newtable(state); int index=0;
                for (const auto& item : std::get<SettingSelection>(value)) {
                    lua_pushlstring(state,item.data(),item.size()); lua_rawseti(state,-2,++index);
                }
            }
            lua_setfield(state,-2,name.c_str());
        }
        lua_newtable(state);
        for (const auto& [name,value]:self.arguments_) {
            std::visit([&](const auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,std::string>)lua_pushlstring(state,v.data(),v.size());
                else if constexpr(std::is_same_v<T,bool>)lua_pushboolean(state,v);
                else if constexpr(std::is_same_v<T,int>)lua_pushinteger(state,v);
                else lua_pushnumber(state,v);
            },value);
            lua_setfield(state,-2,name.c_str());
        }
        lua_call(state,2,1); return 1;
    }
    static int tick(lua_State* state) {
        auto& self=*static_cast<Script*>(lua_touserdata(state,1));
        lua_rawgeti(state,LUA_REGISTRYINDEX,self.module_); raw_field(state,-1,"next");
        if (!lua_isfunction(state,-1)) return luaL_error(state,"Missing next function");
        lua_newtable(state);
        lua_pushinteger(state,self.view_.elapsed.count()); lua_setfield(state,-2,"elapsed_ms");
        lua_pushinteger(state,static_cast<lua_Integer>(self.view_.completed_steps)); lua_setfield(state,-2,"completed_steps");
        if (self.view_.backpack_percent) lua_pushnumber(state,*self.view_.backpack_percent); else lua_pushnil(state);
        lua_setfield(state,-2,"backpack_percent");
        if (self.view_.buffs && self.view_.buffs->value) {
            lua_newtable(state);
            for (const auto& buff:self.view_.buffs->value->buffs) {
                if (std::find(self.buffs_.begin(),self.buffs_.end(),buff.name)==self.buffs_.end()) continue;
                lua_newtable(state);
                if (buff.active) { lua_pushboolean(state,*buff.active); lua_setfield(state,-2,"active"); }
                if (buff.stacks) { lua_pushinteger(state,*buff.stacks); lua_setfield(state,-2,"stacks"); }
                lua_setfield(state,-2,buff.name.c_str());
            }
            lua_setfield(state,-2,"buffs");
            lua_pushinteger(state,(self.view_.context.now-self.view_.buffs->captured).count()); lua_setfield(state,-2,"buffs_age_ms");
        }
        if (self.view_.route_position && self.view_.route_position->usable(self.view_.context)) {
            const auto& position=*self.view_.route_position;
            lua_newtable(state);
            lua_pushlstring(state,position.location.data(),position.location.size()); lua_setfield(state,-2,"location");
            lua_pushstring(state,position.basis==game::PositionBasis::observed?"observed":"route_execution"); lua_setfield(state,-2,"basis");
            lua_pushinteger(state,position.context.now.count()); lua_setfield(state,-2,"established_ms");
        } else lua_pushnil(state);
        lua_setfield(state,-2,"route_position");
        lua_call(state,1,1); return 1;
    }
    static int contribute(lua_State* state) {
        auto& self=*static_cast<Script*>(lua_touserdata(state,1));
        lua_rawgeti(state,LUA_REGISTRYINDEX,self.module_); raw_field(state,-1,"requests");
        if (!lua_isfunction(state,-1)) return luaL_error(state,"Missing requests function");
        const auto& view=self.contributor_view_;
        lua_newtable(state);
        lua_pushinteger(state,view.now.count()); lua_setfield(state,-2,"now_ms");
        lua_pushinteger(state,static_cast<lua_Integer>(view.generation)); lua_setfield(state,-2,"generation");
        if (view.feature) {
            const auto& feature=*view.feature;
            lua_newtable(state);
            lua_pushlstring(state,feature.module_id.data(),feature.module_id.size()); lua_setfield(state,-2,"module_id");
            const char* status="unknown";
            switch (feature.state) {
                case State::ready: status="ready"; break;
                case State::running: status="running"; break;
                case State::paused: status="paused"; break;
                case State::completed: status="completed"; break;
                case State::deferred: status="deferred"; break;
                case State::stopped: status="stopped"; break;
                case State::failed: status="failed"; break;
            }
            lua_pushstring(state,status); lua_setfield(state,-2,"state");
            lua_pushinteger(state,feature.elapsed.count()); lua_setfield(state,-2,"elapsed_ms");
            lua_pushinteger(state,static_cast<lua_Integer>(feature.steps)); lua_setfield(state,-2,"completed_steps");
            if (feature.backpack_percent) lua_pushnumber(state,*feature.backpack_percent); else lua_pushnil(state);
            lua_setfield(state,-2,"backpack_percent");
        } else lua_pushnil(state);
        lua_setfield(state,-2,"feature");
        push_requests(state,view.requests); lua_setfield(state,-2,"requests");
        lua_call(state,1,1); return 1;
    }
    void call(lua_CFunction function) {
        auto* state=vm_.state; lua_settop(state,0);
        vm_.memory.instructions=100; vm_.memory.deadline=monotonic_now()+Time(25);
        vm_.memory.limit=memory_limit;
        lua_sethook(state,budget,LUA_MASKCOUNT,1000);
        lua_pushcfunction(state,function); lua_pushlightuserdata(state,this);
        const auto result=lua_pcall(state,1,1,0); lua_sethook(state,nullptr,0,0);
        vm_.memory.limit=memory_limit+65536;
        if (result!=LUA_OK) {
            const auto* message=lua_type(state,-1)==LUA_TSTRING?lua_tostring(state,-1):nullptr;
            throw std::runtime_error(message?std::string(message).substr(0,500):"Script failed");
        }
        if (!lua_istable(state,-1)) throw std::runtime_error("Script must return a table");
    }
public:
    Script(Sources sources,Settings settings,ScriptMode mode=ScriptMode::program,extensions::CommandArguments arguments={}):sources_(std::move(sources)),settings_(std::move(settings)),arguments_(std::move(arguments)),mode_(mode) {
        if (!sources_.contains("main") || sources_.size()>17) throw std::invalid_argument("Missing main script or too many pattern scripts");
        std::size_t bytes{};
        for (const auto& [name,source] : sources_) { bytes+=source.size(); if (name.size()>80 || source.size()>65536) throw std::invalid_argument("Script exceeds package limits"); }
        if (bytes>262144 || settings_.size()>maximum_settings) throw std::invalid_argument("Feature exceeds package limits");
        if(arguments_.size()>24)throw std::invalid_argument("Too many command arguments");
        for(const auto& [name,value]:arguments_) {
            if(name.empty() || name.size()>32 || name.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid command argument name");
            if(const auto* text=std::get_if<std::string>(&value);text && text->size()>6000)throw std::invalid_argument("Command text is too long");
            if(const auto* real=std::get_if<double>(&value);real && !std::isfinite(*real))throw std::invalid_argument("Command number must be finite");
        }
        call(initialize);
        if (mode_==ScriptMode::contributor) {
            lua_rawgeti(vm_.state,LUA_REGISTRYINDEX,module_); raw_field(vm_.state,-1,"requests");
            if (!lua_isfunction(vm_.state,-1)) throw std::runtime_error("Contributor needs a requests function");
            lua_settop(vm_.state,0); return;
        }
        if (mode_==ScriptMode::pattern) {
            if (has_field(vm_.state,-1,"buffs")) {
                raw_field(vm_.state,-1,"buffs");
                const auto size=dense_list(vm_.state,-1,game::tracked_buffs().size());
                for (std::size_t i=1;i<=size;++i) {
                    lua_rawgeti(vm_.state,-1,static_cast<lua_Integer>(i)); auto name=request_text(vm_.state,-1,80); lua_pop(vm_.state,1);
                    if (std::none_of(game::tracked_buffs().begin(),game::tracked_buffs().end(),[&](const auto& type) { return type.name==name; })
                        || std::find(buffs_.begin(),buffs_.end(),name)!=buffs_.end()) throw std::invalid_argument("Unknown or repeated pattern buff: "+name);
                    buffs_.push_back(std::move(name));
                }
            }
            lua_settop(vm_.state,0); return;
        }
        config_={Time(static_cast<Time::rep>(number_field(vm_.state,-1,"duration_ms",1000,28800000))),number_field(vm_.state,-1,"speed",1,200)};
        raw_field(vm_.state,-1,"backpack_limit"); const bool observed=!lua_isnil(vm_.state,-1); lua_pop(vm_.state,1);
        if (observed) config_.backpack_limit=number_field(vm_.state,-1,"backpack_limit",1,100);
        config_.observe=boolean_field(vm_.state,-1,"observe");
        if (has_field(vm_.state,-1,"routine")) routine_=routine_program(routine_field(vm_.state,config_));
        lua_settop(vm_.state,0);
    }
    game::Requests requests(const ContributorView& view) override {
        if (mode_!=ScriptMode::contributor || !view.generation || view.generation>static_cast<std::uint64_t>(std::numeric_limits<lua_Integer>::max())
            || view.now<Time::zero()) throw std::invalid_argument("Invalid contributor context");
        contributor_view_=view; call(contribute);
        return request_batch(vm_.state);
    }
    RunConfig config() const override {
        if (mode_==ScriptMode::pattern) throw std::logic_error("Patterns require host execution limits");
        return config_;
    }
    void observe(const View& view) override { if (routine_) routine_->observe(view); }
    std::span<const std::string> requested_buffs() const override { return buffs_; }
    Step next(const View& view) override {
        if (mode_==ScriptMode::contributor) throw std::logic_error("Contributors cannot execute programs");
        if (routine_) return routine_->next(view);
        view_=view; call(tick); auto* state=vm_.state;
        return step_value(state,config_.speed);
    }
};
}
std::unique_ptr<Program> script_program(Sources sources,const Settings& settings,extensions::CommandArguments arguments) { return std::make_unique<Script>(std::move(sources),settings,ScriptMode::program,std::move(arguments)); }
std::unique_ptr<Program> script_pattern(Sources sources,const Settings& settings) { return std::make_unique<Script>(std::move(sources),settings,ScriptMode::pattern); }
std::unique_ptr<Contributor> script_contributor(Sources sources,const Settings& settings) { return std::make_unique<Script>(std::move(sources),settings,ScriptMode::contributor); }
}
