#include "movement.hpp"
#include "bundled_files.hpp"
#include "game/movement/camera.hpp"
#include "runtime.hpp"
#include "bundled_movement.hpp"
#include <fstream>
#include <array>
#include <mutex>
#include <cstring>
#include <set>

namespace hive::movement {
namespace {
// Exact shipped defaults predating the per-install baseline; edited files never match.
inline const std::map<std::string,std::set<std::uint64_t>> legacy_defaults{
    {"activities",{0x31f2f80e9182219cULL}},
    {"ant",{0x830de97e81f91ff9ULL,0xc93966454a049eedULL}},
    {"ant-position",{0x3d7e96dccfaf1a38ULL}},
    {"boosters",{0x6c7c933d58fa666fULL}},
    {"bosses",{0xabee444e04060212ULL,0xe7d89875c9fb85d2ULL}},
    {"cannon",{0x1333ca5877a68d4cULL}},
    {"combat",{0x1ccbd3e6cc7896e9ULL}},
    {"dispensers",{0x1f9ec94361865d8dULL}},
    {"fields",{0x617329d84e154b41ULL}},
    {"gummy-entry",{0xb9ad510143252f63ULL}},
    {"helpers",{0x268a1989c39ffbd5ULL,0xd049d4ed3b8c2d3eULL}},
    {"hive",{0x13d4ab1bea02d423ULL}},
    {"items",{0xe6c6915c7fcefcb3ULL}},
    {"memory",{0xa5f175a862d999c9ULL}},
    {"npc",{0x18dc036901ecdad3ULL}},
    {"petal-loot",{0x19aeac775dc6243aULL}},
    {"planter-loot",{0x3c0900fc8bc3c11aULL}},
    {"planter-search",{0x5dd228aed279587eULL}},
    {"planting",{0x426081789ecfe3f7ULL}},
    {"return-route",{0x91b5bc7d666224c8ULL}},
    {"returns",{0xf00e759e50a0ff78ULL}},
    {"shrine-loot",{0x4f011ea3addba2b8ULL}},
    {"sprinklers",{0x4a89cf2f5d5c76a0ULL}},
    {"sticker-stack",{0xb44d70bd5c13d904ULL}},
    {"vicious-search",{0x5e549bf5eb101b3aULL}},
};
void field(lua_State* s,int i,const char* name) { i=lua_absindex(s,i); lua_pushstring(s,name); lua_rawget(s,i); }
std::string text(lua_State* s,int i) {
    if (lua_type(s,i)!=LUA_TSTRING) throw std::runtime_error("Movement value must be text");
    std::size_t n{}; const auto* value=lua_tolstring(s,i,&n);
    if (n>160 || std::memchr(value,0,n)) throw std::runtime_error("Invalid movement text");
    return {value,n};
}
std::string text(lua_State* s,int i,const char* name,const char* fallback="") {
    field(s,i,name); auto result=lua_isnil(s,-1)?std::string(fallback):text(s,-1); lua_pop(s,1); return result;
}
double number(lua_State* s,int i,double low,double high) {
    if (lua_type(s,i)!=LUA_TNUMBER) throw std::runtime_error("Movement value must be a number");
    const auto n=lua_tonumber(s,i);
    if (!std::isfinite(n) || n<low || n>high) throw std::runtime_error("Movement value outside limits");
    return n;
}
double number(lua_State* s,int i,const char* name,double fallback,double low,double high) {
    field(s,i,name); const auto result=lua_isnil(s,-1)?fallback:number(s,-1,low,high); lua_pop(s,1); return result;
}
bool flag(lua_State* s,int i,const char* name,bool fallback=false) {
    field(s,i,name);
    if (!lua_isnil(s,-1) && !lua_isboolean(s,-1)) throw std::runtime_error("Movement flag must be boolean");
    const auto result=lua_isnil(s,-1)?fallback:bool(lua_toboolean(s,-1)); lua_pop(s,1); return result;
}
std::size_t list(lua_State* s,int i,std::size_t max) {
    i=lua_absindex(s,i);
    if (!lua_istable(s,i) || lua_rawlen(s,i)>max) throw std::runtime_error("Movement list outside limits");
    const auto size=lua_rawlen(s,i); lua_pushnil(s);
    while (lua_next(s,i)) {
        if (!lua_isinteger(s,-2) || lua_tointeger(s,-2)<1 || std::size_t(lua_tointeger(s,-2))>size)
            throw std::runtime_error("Movement needs a dense list");
        lua_pop(s,1);
    }
    return size;
}
Key key(lua_State* s,int i) {
    const auto name=text(s,i);
    static const std::map<std::string,Key> keys{{"forward",Key::forward},{"backward",Key::backward},{"left",Key::left},{"right",Key::right},
        {"jump",Key::jump},{"rotate_left",Key::rotate_left},{"rotate_right",Key::rotate_right},{"pitch_up",Key::pitch_up},{"pitch_down",Key::pitch_down},
        {"zoom_in",Key::zoom_in},{"zoom_out",Key::zoom_out},{"shift_lock",Key::shift_lock}};
    const auto found=keys.find(name); if (found==keys.end()) throw std::runtime_error("Unknown movement key: "+name);
    return found->second;
}
std::vector<Key> keys(lua_State* s,int i,const char* name) {
    std::vector<Key> result; field(s,i,name);
    if (!lua_isnil(s,-1)) for (std::size_t n=1,count=list(s,-1,8);n<=count;++n) {
        lua_rawgeti(s,-1,n); result.push_back(key(s,-1)); lua_pop(s,1);
    }
    lua_pop(s,1); return result;
}
game::WalkLeg leg(lua_State* s,int i) {
    i=lua_absindex(s,i); if (!lua_istable(s,i)) throw std::runtime_error("Movement leg must be a table");
    game::WalkLeg result;
    lua_rawgeti(s,i,1); result.tiles=number(s,-1,0.001,500); lua_pop(s,1);
    lua_rawgeti(s,i,2); result.first=key(s,-1); lua_pop(s,1);
    lua_rawgeti(s,i,3); if (!lua_isnil(s,-1)) result.second=key(s,-1); lua_pop(s,1);
    lua_rawgeti(s,i,4);
    if (!lua_isnil(s,-1) && !lua_isboolean(s,-1)) throw std::runtime_error("Jump flag must be boolean");
    result.jump=lua_toboolean(s,-1); lua_pop(s,1);
    game::movement_slice(result,result.tiles,28); return result;
}
std::vector<game::WalkLeg> legs(lua_State* s,int i,const char* name) {
    field(s,i,name); std::vector<game::WalkLeg> result;
    if (!lua_isnil(s,-1)) for (std::size_t n=1,count=list(s,-1,128);n<=count;++n) {
        lua_rawgeti(s,-1,n); result.push_back(leg(s,-1)); lua_pop(s,1);
    }
    lua_pop(s,1); return result;
}
Time ms(lua_State* s,int i,const char* name,Time fallback,Time high=Time(300000)) {
    const auto value=number(s,i,name,double(fallback.count()),0,double(high.count()));
    if (std::floor(value)!=value) throw std::runtime_error("Movement timing must be whole milliseconds");
    return Time(static_cast<Time::rep>(value));
}
game::CameraMove camera_move(lua_State* s,int i) {
    i=lua_absindex(s,i);
    field(s,i,"key"); const auto direction=key(s,-1); lua_pop(s,1);
    const auto count=number(s,i,"count",2,1,8); if (std::floor(count)!=count) throw std::runtime_error("Invalid turn count");
    game::CameraMove move{direction,static_cast<unsigned>(count)};
    move.press=ms(s,i,"press",move.press); move.release=ms(s,i,"release",move.release);
    game::camera_actions(move); return move;
}
Action action(lua_State* s,int i) {
    Action result{text(s,i,"label","Movement"),keys(s,i,"keys"),ms(s,i,"ms",Time(0),Time(5000)),{}};
    field(s,i,"tap"); if (!lua_isnil(s,-1)) result.tap=key(s,-1); lua_pop(s,1);
    const auto count=number(s,i,"tap_count",1,1,8); if (std::floor(count)!=count) throw std::runtime_error("Invalid turn count");
    result.tap_count=static_cast<unsigned>(count); return result;
}
std::vector<Action> actions(lua_State* s,int i,const char* name) {
    field(s,i,name); std::vector<Action> result;
    if (!lua_isnil(s,-1)) for (std::size_t n=1,count=list(s,-1,512);n<=count;++n) {
        lua_rawgeti(s,-1,n); result.push_back(action(s,-1)); lua_pop(s,1);
    }
    lua_pop(s,1); return result;
}
workflows::CheckpointPlan checkpoint(lua_State* s,int i,double speed) {
    i=lua_absindex(s,i); const auto kind=text(s,i,"kind"); workflows::CheckpointPlan p;
    if (kind=="walk") {
        p.path=legs(s,i,"legs"); p.speed=speed; p.attempts=0; p.completion=workflows::CheckpointCompletion::movement;
        double distance=0; for (const auto& l:p.path) distance+=l.tiles;
        p.timeout=Time(static_cast<Time::rep>(std::ceil(distance*4000/speed)))+Time(5000);
    } else if (kind=="turn") {
        const auto timing=camera_move(s,i);
        p.camera_settle=timing.release; p.attempts=0; p.completion=workflows::CheckpointCompletion::movement;
        for (unsigned n=0;n<timing.steps;++n) p.approach.push_back({"Route turn",{timing.key},timing.press,{}});
    } else if (kind=="motion") {
        p.continuous=true; p.attempts=0; p.completion=workflows::CheckpointCompletion::movement; p.timeout=Time(5000); p.speed=speed;
        field(s,i,"steps");
        for (std::size_t n=1,count=list(s,-1,128);n<=count;++n) {
            lua_rawgeti(s,-1,n); field(s,-1,"ms"); const bool hold=!lua_isnil(s,-1); lua_pop(s,1);
            if (hold) {
                auto a=action(s,-1); a.label="Traverse ledge"; p.timeout+=a.duration;
                for (auto remaining=a.duration;remaining>Time{};) { a.duration=std::min(remaining,Time(250)); p.sequence.push_back(a); remaining-=a.duration; }
            } else { auto l=leg(s,-1); p.sequence.push_back(l); p.timeout+=Time(static_cast<Time::rep>(std::ceil(l.tiles*4000/speed))); }
            lua_pop(s,1);
        }
        lua_pop(s,1);
    } else if (kind=="checkpoint") {
        p.detector=text(s,i,"detector"); p.timeout=ms(s,i,"timeout",p.timeout); p.observation_age=ms(s,i,"age",p.observation_age);
        const auto attempts=number(s,i,"attempts",p.attempts,0,16); if (std::floor(attempts)!=attempts) throw std::runtime_error("Invalid attempt count");
        p.attempts=static_cast<std::size_t>(attempts); p.continuous=flag(s,i,"continuous");
        p.camera_settle=ms(s,i,"settle",p.camera_settle);
        const auto completion=text(s,i,"completion","observation");
        if (completion=="movement") p.completion=workflows::CheckpointCompletion::movement;
        else if (completion=="launch") p.completion=workflows::CheckpointCompletion::launch;
        else if (completion!="observation") throw std::runtime_error("Unknown movement completion");
        if (flag(s,i,"use_speed")) p.speed=speed;
        p.approach=actions(s,i,"approach"); p.nudge=actions(s,i,"nudge"); p.path=legs(s,i,"path"); p.nudge_path=legs(s,i,"nudge_path");
        p.launch_movement=keys(s,i,"launch_movement");
        field(s,i,"launch_prepare");
        if (!lua_isnil(s,-1)) for (std::size_t n=1,count=list(s,-1,16);n<=count;++n) {
            lua_rawgeti(s,-1,n); auto turn=game::camera_actions(camera_move(s,-1)); lua_pop(s,1);
            p.launch_prepare.insert(p.launch_prepare.end(),turn.begin(),turn.end());
        }
        lua_pop(s,1);
    } else throw std::runtime_error("Unknown movement step: "+kind);
    p.observation_scope=text(s,i,"scope");
    p.minimum_duration=ms(s,i,"minimum_duration",p.minimum_duration);
    workflows::validate(p); return p;
}
std::map<std::string,std::string> read_sources(const std::filesystem::path& directory) {
    if (std::filesystem::is_symlink(directory) || !std::filesystem::is_directory(directory)) throw std::runtime_error("Movement folder must be a regular directory");
    std::map<std::string,std::string> result;
    std::size_t entries=0;
    std::array<char,131073> buffer;
    for (const auto& entry:std::filesystem::directory_iterator(directory)) {
        if (++entries>256) throw std::runtime_error("Movement folder contains too many entries");
        if (entry.path().extension()!=".lua") continue;
        if (result.size()>=64 || entry.is_symlink() || !entry.is_regular_file()) throw std::runtime_error("Invalid movement file");
        std::ifstream input(entry.path(),std::ios::binary);
        input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
        const auto size=static_cast<std::size_t>(input.gcount());
        if (input.bad() || size==0 || size>131072) throw std::runtime_error("Cannot read movement file: "+entry.path().filename().string());
        result.emplace(entry.path().stem().string(),std::string(buffer.data(),size));
    }
    return result;
}
}
struct Recipes::Impl {
    scripting::detail::Vm vm;
    std::map<std::string,std::string> sources;
    mutable std::mutex mutex;
    std::string file;
    Parameters parameters;
    static int require(lua_State* s) {
        auto& self=*static_cast<Impl*>(lua_touserdata(s,lua_upvalueindex(1)));
        const char* name=luaL_checkstring(s,1); const auto found=self.sources.find(name);
        if (found==self.sources.end()) return luaL_error(s,"Missing movement file: %s",name);
        lua_getfield(s,LUA_REGISTRYINDEX,"movement.modules"); lua_getfield(s,-1,name);
        if (lua_isboolean(s,-1)) return luaL_error(s,"Circular movement import: %s",name);
        if (!lua_isnil(s,-1)) return 1;
        lua_pop(s,1); lua_pushboolean(s,1); lua_setfield(s,-2,name);
        if (luaL_loadbufferx(s,found->second.data(),found->second.size(),name,"t")!=LUA_OK) return lua_error(s);
        lua_call(s,0,1); lua_pushvalue(s,-1); lua_setfield(s,-3,name); return 1;
    }
    static int initialize(lua_State* s) {
        auto* self=static_cast<Impl*>(lua_touserdata(s,1));
        luaL_requiref(s,"_G",luaopen_base,1); lua_pop(s,1);
        for (const auto* name:{"dofile","loadfile","load","collectgarbage","print","warn","pcall","xpcall","setmetatable","getmetatable"}) {lua_pushnil(s);lua_setglobal(s,name);}
        luaL_requiref(s,"math",luaopen_math,1); lua_pop(s,1);
        luaL_requiref(s,"table",luaopen_table,1); lua_pop(s,1);
        luaL_requiref(s,"string",luaopen_string,1); lua_pop(s,1);
        lua_pushlightuserdata(s,self); lua_pushcclosure(s,require,1); lua_setglobal(s,"require");
        lua_newtable(s); lua_setfield(s,LUA_REGISTRYINDEX,"movement.modules");
        return 0;
    }
    static int invoke(lua_State* s) {
        auto& self=*static_cast<Impl*>(lua_touserdata(s,1));
        lua_getglobal(s,"require"); lua_pushlstring(s,self.file.data(),self.file.size()); lua_call(s,1,1);
        lua_newtable(s);
        for (const auto& [name,value]:self.parameters) {
            std::visit([&](const auto& v) {
                using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,bool>) lua_pushboolean(s,v);
                else if constexpr(std::is_same_v<T,std::string>) lua_pushlstring(s,v.data(),v.size());
                else lua_pushnumber(s,v);
            },value);
            lua_setfield(s,-2,name.c_str());
        }
        lua_call(s,1,1); return 1;
    }
    void call(lua_CFunction fn,int results) {
        auto* s=vm.state; lua_settop(s,0); vm.memory.limit=scripting::detail::memory_limit; vm.memory.instructions=1000; vm.memory.deadline=monotonic_now()+Time(100);
        lua_sethook(s,scripting::detail::budget,LUA_MASKCOUNT,1000);
        lua_pushcfunction(s,fn); lua_pushlightuserdata(s,this);
        const int outcome=lua_pcall(s,1,results,0); lua_sethook(s,nullptr,0,0);
        vm.memory.limit=scripting::detail::memory_limit+65536;
        if (outcome!=LUA_OK) { const char* error=lua_tostring(s,-1); throw std::runtime_error(file+": "+(error?error:"Movement script failed")); }
    }
};
Recipes::Recipes(std::map<std::string,std::string> sources):impl_(std::make_unique<Impl>()) { impl_->sources=std::move(sources); impl_->call(Impl::initialize,0); }
Recipes::~Recipes()=default;
std::vector<Step> Recipes::sequence(const std::string& file,const Parameters& parameters,double speed) const {
    if (!std::isfinite(speed) || speed<8 || speed>200) throw std::invalid_argument("Movement needs a valid speed");
    std::lock_guard lock(impl_->mutex); impl_->file=file; impl_->parameters=parameters; impl_->call(Impl::invoke,1);
    auto* s=impl_->vm.state; std::vector<Step> result;
    for (std::size_t n=1,count=list(s,-1,64);n<=count;++n) {
        lua_rawgeti(s,-1,n);
        if (text(s,-1,"kind")=="shift_lock") result.push_back(ShiftLock{flag(s,-1,"enabled")});
        else if (text(s,-1,"kind")=="marker") result.push_back(Marker{text(s,-1,"name")});
        else result.push_back(checkpoint(s,-1,speed));
        lua_pop(s,1);
    }
    return result;
}
std::vector<workflows::CheckpointPlan> Recipes::path(const std::string& file,const Parameters& parameters,double speed) const {
    std::vector<workflows::CheckpointPlan> result;
    for (auto& step:sequence(file,parameters,speed)) {
        if (!std::holds_alternative<workflows::CheckpointPlan>(step)) throw std::runtime_error("Navigation routes only support movement checkpoints");
        result.push_back(std::get<workflows::CheckpointPlan>(std::move(step)));
    }
    return result;
}
std::vector<game::WalkLeg> Recipes::legs(const std::string& file,const Parameters& parameters) const {
    std::lock_guard lock(impl_->mutex); impl_->file=file; impl_->parameters=parameters; impl_->call(Impl::invoke,1);
    auto* s=impl_->vm.state; std::vector<game::WalkLeg> result;
    for (std::size_t n=1,count=list(s,-1,128);n<=count;++n) { lua_rawgeti(s,-1,n); result.push_back(leg(s,-1)); lua_pop(s,1); }
    return result;
}
struct Library::Impl {
    std::filesystem::path directory;
    std::map<std::string,std::string> loaded;
    std::shared_ptr<const Recipes> recipes;
    std::mutex mutex;
};
Library::Library(std::filesystem::path directory):impl_(std::make_shared<Impl>()) {
    impl_->directory=std::move(directory);
    const auto errors=scripting::install_bundled_files(impl_->directory,bundled_movement,legacy_defaults);
    if (!errors.empty()) throw std::runtime_error(errors.front());
}
std::shared_ptr<const Recipes> Library::snapshot() const {
    std::lock_guard lock(impl_->mutex); auto sources=read_sources(impl_->directory);
    if (!impl_->recipes || sources!=impl_->loaded) {
        auto recipes=std::make_shared<Recipes>(sources);
        impl_->loaded=std::move(sources); impl_->recipes=std::move(recipes);
    }
    return impl_->recipes;
}
std::shared_ptr<const Recipes> snapshot(const Source& source) {
    if (source) return source->snapshot();
    static const auto bundled=std::make_shared<const Recipes>(bundled_movement);
    return bundled;
}
}
