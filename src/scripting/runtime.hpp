#pragma once
#include "hive/core/model.hpp"
#include <cstdlib>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace hive::scripting::detail {
inline constexpr std::size_t memory_limit=4*1024*1024;
struct Memory { std::size_t used{},limit{memory_limit}; int instructions{}; Time deadline{}; };
inline void* allocate(void* data,void* pointer,std::size_t old_size,std::size_t size) {
    auto& memory=*static_cast<Memory*>(data); if (!pointer) old_size=0;
    if (!size) { std::free(pointer); memory.used-=old_size; return nullptr; }
    if (size>memory.limit || memory.used-old_size>memory.limit-size) return nullptr;
    auto* replacement=std::realloc(pointer,size);
    if (replacement) memory.used=memory.used-old_size+size;
    return replacement;
}
inline void budget(lua_State* state,lua_Debug*) {
    void* data{}; lua_getallocf(state,&data); auto& memory=*static_cast<Memory*>(data);
    if (--memory.instructions<=0 || monotonic_now()>memory.deadline) luaL_error(state,"Script execution budget exceeded");
}
struct Vm {
    Memory memory;
    lua_State* state{lua_newstate(allocate,&memory)};
    Vm() { if (!state) throw std::runtime_error("Cannot allocate script runtime"); }
    ~Vm() { lua_close(state); }
    Vm(const Vm&)=delete;
    Vm& operator=(const Vm&)=delete;
};
}
