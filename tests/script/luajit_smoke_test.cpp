// Phase 27 Day 445: the vendored LuaJIT builds, links and runs a chunk. It names LuaJIT directly (allowed by
// tools/luajit_boundary_check/allowlist.txt); the backend's own tests, from Day 448, go through the script module.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "types.h"

#include <lua.hpp>

using namespace engine_placeholder;

namespace
{
void* CountingAlloc(void* ud, void* ptr, usize old_size, usize new_size)
{
    usize* used = static_cast<usize*>(ud);
    if (new_size == 0)
    {
        if (ptr != nullptr)
        {
            *used -= old_size;
            MemFree(ptr);
        }
        return nullptr;
    }
    void* grown = MemReallocate(ptr, new_size);
    if (grown != nullptr)
    {
        *used = *used - (ptr != nullptr ? old_size : 0) + new_size;
    }
    return grown;
}
}  // namespace

TEST_CASE("LuaJIT runs a chunk and returns its result", "[script][luajit]")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    REQUIRE(luaL_loadstring(L, "return 1 + 1") == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    CHECK(lua_tonumber(L, -1) == 2.0);
    lua_close(L);
}

TEST_CASE("LuaJIT reports its version", "[script][luajit]")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    REQUIRE(luaL_loadstring(L, "return jit.version, jit.version_num") == 0);
    REQUIRE(lua_pcall(L, 0, 2, 0) == 0);
    const char* version = lua_tostring(L, -2);
    REQUIRE(version != nullptr);
    CHECK(CStrCompareN(version, "LuaJIT 2.1.", 11) == 0);
    CHECK(lua_tointeger(L, -1) == LUAJIT_VERSION_NUM);
    CHECK(LUAJIT_VERSION_NUM >= 20199);  // the rolling release 2.1.<commit time> is far above the 2.1.0 numbering
    lua_close(L);
}

TEST_CASE("LuaJIT accepts the engine's allocator (64-bit GC objects) and frees everything", "[script][luajit]")
{
    usize used = 0;
    lua_State* L = lua_newstate(CountingAlloc, &used);
    REQUIRE(L != nullptr);
    CHECK(used > 0);
    REQUIRE(luaL_loadstring(L, "local t = {} for i = 1, 1000 do t[i] = i end return #t") == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    CHECK(lua_tointeger(L, -1) == 1000);
    lua_close(L);
    CHECK(used == 0);
}

TEST_CASE("A script error comes back as a status, not a crash", "[script][luajit]")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    REQUIRE(luaL_loadstring(L, "error('boom')") == 0);
    CHECK(lua_pcall(L, 0, 0, 0) == LUA_ERRRUN);
    lua_close(L);
}

#if defined(LUAJIT_DISABLE_JIT)
TEST_CASE("Built without the JIT, jit.status reports it off", "[script][luajit]")
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    REQUIRE(luaL_loadstring(L, "return (jit.status())") == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    CHECK(lua_toboolean(L, -1) == 0);
    lua_close(L);
}
#endif
