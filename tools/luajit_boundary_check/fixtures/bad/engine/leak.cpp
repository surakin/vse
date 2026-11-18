#include <lua.h>
#include "third_party/luajit/src/lauxlib.h"
int Run() { lua_State* L = luaL_newstate(); return L != nullptr; }
