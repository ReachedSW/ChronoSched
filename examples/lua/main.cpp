#include "chronosched/scheduler.hpp"
#include <lua.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    lua_State* lua = luaL_newstate();
    if (!lua) return 1;
    luaL_openlibs(lua);
    chronosched::Scheduler scheduler;
    scheduler.schedule_after(std::chrono::seconds(1), [lua] {
        if (luaL_dostring(lua, "print('ChronoSched event fired')") != LUA_OK) std::cerr << lua_tostring(lua, -1) << '\n';
    });
    std::this_thread::sleep_for(std::chrono::seconds(2));
    lua_close(lua);
}
