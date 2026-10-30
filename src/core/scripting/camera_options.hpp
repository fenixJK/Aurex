#pragma once
#include "game/movement/camera.hpp"
#include "runtime.hpp"
#include <string_view>

namespace hive::scripting::detail {
// Patterns and routes use the same options; key names/counts belong to their
// respective script formats. Existing explicit input/timing keeps its pacing.
inline void camera_options(lua_State* state,int index,game::CameraMove& move) {
    index=lua_absindex(state,index);
    const auto field=[&](const char* name) { lua_pushstring(state,name); lua_rawget(state,index); };
    field("input");
    if (!lua_isnil(state,-1)) {
        if (lua_type(state,-1)!=LUA_TSTRING) throw std::runtime_error("Camera input must be a string");
        const std::string_view input=lua_tostring(state,-1);
        if (input=="automatic") move.input=game::CameraInput::automatic;
        else if (input=="held") move.input=game::CameraInput::held;
        else if (input=="tapped") move.input=game::CameraInput::tapped;
        else throw std::runtime_error("Camera input must be automatic, held or tapped");
        if (move.input!=game::CameraInput::automatic) move.finish=game::CameraFinish::input_only;
    }
    lua_pop(state,1);
    for (const auto* name:{"press","release"}) {
        field(name);
        if (!lua_isnil(state,-1)) {
            if (lua_type(state,-1)!=LUA_TNUMBER || !std::isfinite(lua_tonumber(state,-1))
                || lua_tonumber(state,-1)!=std::floor(lua_tonumber(state,-1))
                || lua_tonumber(state,-1)<20 || lua_tonumber(state,-1)>250)
                throw std::runtime_error("Camera key timing must be whole milliseconds from 20 to 250");
            if (!move.hold) move.hold=game::CameraHold{};
            (std::string_view(name)=="press"?move.hold->press:move.hold->release)=Time(lua_tointeger(state,-1));
            move.finish=game::CameraFinish::input_only;
        }
        lua_pop(state,1);
    }
    field("finish");
    if (!lua_isnil(state,-1)) {
        if (lua_type(state,-1)!=LUA_TSTRING) throw std::runtime_error("Camera finish must be a string");
        const std::string_view finish=lua_tostring(state,-1);
        if (finish=="input-only") move.finish=game::CameraFinish::input_only;
        else if (finish=="movement") move.finish=game::CameraFinish::movement;
        else if (finish=="observation") move.finish=game::CameraFinish::observation;
        else if (finish=="geometry") move.finish=game::CameraFinish::geometry;
        else throw std::runtime_error("Unknown camera completion policy");
    }
    lua_pop(state,1);
    game::camera_actions(move);
}
}
