// A port of the pattern matcher of Lua 5.1's lstrlib.c (MIT license, Lua.org, PUC-Rio) with a step budget and ASCII
// character classes that do not depend on the C locale. Frames here hold no objects with destructors: errors unwind the
// C stack (see luajit_backend.cpp).
#include "script/luajit/lua_patterns.h"

#include "cstr/mem_ops.h"
#include "types.h"

#include <lua.hpp>

namespace engine_placeholder::script::luajit
{

namespace
{

constexpr int kCapUnfinished = -1;
constexpr int kCapPosition = -2;
constexpr int kMaxCaptures = 32;
constexpr int kMaxDepth = 200;
constexpr long kMaxSteps = 4000000;  // matcher steps in one call to find, match, gmatch's next or gsub
constexpr char kEsc = '%';

struct MatchState
{
    const char* src_init;
    const char* src_end;
    lua_State* L;
    int level;
    int depth;
    long steps;
    struct
    {
        const char* init;
        isize len;
    } capture[kMaxCaptures];
};

bool IsAlpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool IsDigit(int c)
{
    return c >= '0' && c <= '9';
}
bool IsLower(int c)
{
    return c >= 'a' && c <= 'z';
}
bool IsUpper(int c)
{
    return c >= 'A' && c <= 'Z';
}
bool IsSpace(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}
bool IsCntrl(int c)
{
    return (c >= 0 && c < 32) || c == 127;
}
bool IsPunct(int c)
{
    return c > 32 && c < 127 && !IsAlpha(c) && !IsDigit(c);
}
bool IsXDigit(int c)
{
    return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int ToLower(int c)
{
    return IsUpper(c) ? c + 32 : c;
}

int CheckCapture(MatchState* ms, int l)
{
    l -= '1';
    if (l < 0 || l >= ms->level || ms->capture[l].len == kCapUnfinished)
    {
        return luaL_error(ms->L, "invalid capture index");
    }
    return l;
}

int CaptureToClose(MatchState* ms)
{
    int level = ms->level;
    for (level--; level >= 0; level--)
    {
        if (ms->capture[level].len == kCapUnfinished)
        {
            return level;
        }
    }
    return luaL_error(ms->L, "invalid pattern capture");
}

const char* ClassEnd(MatchState* ms, const char* p)
{
    switch (*p++)
    {
    case kEsc:
        if (*p == '\0')
        {
            luaL_error(ms->L, "malformed pattern (ends with '%%')");
        }
        return p + 1;
    case '[':
        if (*p == '^')
        {
            p++;
        }
        do
        {
            if (*p == '\0')
            {
                luaL_error(ms->L, "malformed pattern (missing ']')");
            }
            if (*(p++) == kEsc && *p != '\0')
            {
                p++;
            }
        } while (*p != ']');
        return p + 1;
    default: return p;
    }
}

bool MatchClass(int c, int cl)
{
    bool res;
    switch (ToLower(cl))
    {
    case 'a': res = IsAlpha(c); break;
    case 'c': res = IsCntrl(c); break;
    case 'd': res = IsDigit(c); break;
    case 'l': res = IsLower(c); break;
    case 'p': res = IsPunct(c); break;
    case 's': res = IsSpace(c); break;
    case 'u': res = IsUpper(c); break;
    case 'w': res = IsAlpha(c) || IsDigit(c); break;
    case 'x': res = IsXDigit(c); break;
    default: return cl == c;
    }
    if (IsUpper(cl))
    {
        res = !res;
    }
    return res;
}

bool MatchBracketClass(int c, const char* p, const char* ec)
{
    bool sig = true;
    if (*(p + 1) == '^')
    {
        sig = false;
        p++;
    }
    while (++p < ec)
    {
        if (*p == kEsc)
        {
            p++;
            if (MatchClass(c, static_cast<u8>(*p)))
            {
                return sig;
            }
        }
        else if (*(p + 1) == '-' && (p + 2 < ec))
        {
            p += 2;
            if (static_cast<u8>(*(p - 2)) <= c && c <= static_cast<u8>(*p))
            {
                return sig;
            }
        }
        else if (static_cast<u8>(*p) == c)
        {
            return sig;
        }
    }
    return !sig;
}

bool SingleMatch(MatchState* ms, const char* s, const char* p, const char* ep)
{
    if (s >= ms->src_end)
    {
        return false;
    }
    const int c = static_cast<u8>(*s);
    switch (*p)
    {
    case '.': return true;
    case kEsc: return MatchClass(c, static_cast<u8>(*(p + 1)));
    case '[': return MatchBracketClass(c, p, ep - 1);
    default: return static_cast<u8>(*p) == c;
    }
}

const char* Match(MatchState* ms, const char* s, const char* p);

const char* MatchBalance(MatchState* ms, const char* s, const char* p)
{
    if (*p == 0 || *(p + 1) == 0)
    {
        luaL_error(ms->L, "unbalanced pattern");
    }
    if (s >= ms->src_end || *s != *p)
    {
        return nullptr;
    }
    const int b = *p;
    const int e = *(p + 1);
    int cont = 1;
    while (++s < ms->src_end)
    {
        if (--ms->steps < 0)
        {
            luaL_error(ms->L, "pattern too complex");
        }
        if (*s == e)
        {
            if (--cont == 0)
            {
                return s + 1;
            }
        }
        else if (*s == b)
        {
            cont++;
        }
    }
    return nullptr;
}

const char* MaxExpand(MatchState* ms, const char* s, const char* p, const char* ep)
{
    isize i = 0;
    while (SingleMatch(ms, s + i, p, ep))
    {
        i++;
        if (--ms->steps < 0)
        {
            luaL_error(ms->L, "pattern too complex");
        }
    }
    while (i >= 0)
    {
        const char* res = Match(ms, s + i, ep + 1);
        if (res != nullptr)
        {
            return res;
        }
        i--;
    }
    return nullptr;
}

const char* MinExpand(MatchState* ms, const char* s, const char* p, const char* ep)
{
    for (;;)
    {
        const char* res = Match(ms, s, ep + 1);
        if (res != nullptr)
        {
            return res;
        }
        if (SingleMatch(ms, s, p, ep))
        {
            s++;
        }
        else
        {
            return nullptr;
        }
    }
}

const char* StartCapture(MatchState* ms, const char* s, const char* p, int what)
{
    const int level = ms->level;
    if (level >= kMaxCaptures)
    {
        luaL_error(ms->L, "too many captures");
    }
    ms->capture[level].init = s;
    ms->capture[level].len = what;
    ms->level = level + 1;
    const char* res = Match(ms, s, p);
    if (res == nullptr)
    {
        ms->level--;
    }
    return res;
}

const char* EndCapture(MatchState* ms, const char* s, const char* p)
{
    const int l = CaptureToClose(ms);
    ms->capture[l].len = s - ms->capture[l].init;
    const char* res = Match(ms, s, p);
    if (res == nullptr)
    {
        ms->capture[l].len = kCapUnfinished;
    }
    return res;
}

const char* MatchCapture(MatchState* ms, const char* s, int l)
{
    l = CheckCapture(ms, l);
    const usize len = static_cast<usize>(ms->capture[l].len);
    ms->steps -= static_cast<long>(len / 16);  // comparing a long capture is real work
    if (ms->steps < 0)
    {
        luaL_error(ms->L, "pattern too complex");
    }
    if (static_cast<usize>(ms->src_end - s) >= len && MemCompare(ms->capture[l].init, s, len) == 0)
    {
        return s + len;
    }
    return nullptr;
}

const char* Match(MatchState* ms, const char* s, const char* p)
{
    if (++ms->depth > kMaxDepth || --ms->steps < 0)
    {
        luaL_error(ms->L, "pattern too complex");
    }
    const char* result = nullptr;
init:
    switch (*p)
    {
    case '(':
        if (*(p + 1) == ')')
        {
            result = StartCapture(ms, s, p + 2, kCapPosition);
        }
        else
        {
            result = StartCapture(ms, s, p + 1, kCapUnfinished);
        }
        break;
    case ')': result = EndCapture(ms, s, p + 1); break;
    case kEsc:
        switch (*(p + 1))
        {
        case 'b':
            s = MatchBalance(ms, s, p + 2);
            if (s != nullptr)
            {
                p += 4;
                goto init;
            }
            result = nullptr;
            break;
        case 'f':
        {
            p += 2;
            if (*p != '[')
            {
                luaL_error(ms->L, "missing '[' after '%%f' in pattern");
            }
            const char* ep = ClassEnd(ms, p);
            const char previous = (s == ms->src_init) ? '\0' : *(s - 1);
            if (!MatchBracketClass(static_cast<u8>(previous), p, ep - 1) &&
                MatchBracketClass(static_cast<u8>(s < ms->src_end ? *s : '\0'), p, ep - 1))
            {
                p = ep;
                goto init;
            }
            result = nullptr;
            break;
        }
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            s = MatchCapture(ms, s, static_cast<u8>(*(p + 1)));
            if (s != nullptr)
            {
                p += 2;
                goto init;
            }
            result = nullptr;
            break;
        default: goto dflt;
        }
        break;
    case '\0': result = s; break;
    case '$':
        if (*(p + 1) == '\0')
        {
            result = (s == ms->src_end) ? s : nullptr;
            break;
        }
        goto dflt;
    default:
    dflt:
        {
            const char* ep = ClassEnd(ms, p);
            const bool single = SingleMatch(ms, s, p, ep);
            switch (*ep)
            {
            case '?':
            {
                const char* res;
                if (single && (res = Match(ms, s + 1, ep + 1)) != nullptr)
                {
                    result = res;
                }
                else
                {
                    p = ep + 1;
                    goto init;
                }
                break;
            }
            case '*': result = MaxExpand(ms, s, p, ep); break;
            case '+': result = single ? MaxExpand(ms, s + 1, p, ep) : nullptr; break;
            case '-': result = MinExpand(ms, s, p, ep); break;
            default:
                if (!single)
                {
                    result = nullptr;
                }
                else
                {
                    s++;
                    p = ep;
                    goto init;
                }
                break;
            }
            break;
        }
    }
    ms->depth--;
    return result;
}

const char* FindPlain(lua_State* L, const char* s1, usize l1, const char* s2, usize l2)
{
    long steps = kMaxSteps;
    if (l2 == 0)
    {
        return s1;
    }
    if (l2 > l1)
    {
        return nullptr;
    }
    const char* init;
    l2--;
    l1 = l1 - l2;
    while (l1 > 0)
    {
        init = nullptr;
        for (usize i = 0; i < l1; ++i)
        {
            if (s1[i] == *s2)
            {
                init = s1 + i;
                break;
            }
        }
        if (init == nullptr)
        {
            return nullptr;
        }
        init++;
        steps -= 1 + static_cast<long>(l2 / 16);  // each candidate position costs a comparison of the rest
        if (steps < 0)
        {
            luaL_error(L, "pattern too complex");
        }
        if (MemCompare(init, s2 + 1, l2) == 0)
        {
            return init - 1;
        }
        l1 -= static_cast<usize>(init - s1);
        s1 = init;
    }
    return nullptr;
}

void PushOneCapture(MatchState* ms, int i, const char* s, const char* e)
{
    if (i >= ms->level)
    {
        if (i == 0)
        {
            lua_pushlstring(ms->L, s, static_cast<usize>(e - s));
        }
        else
        {
            luaL_error(ms->L, "invalid capture index");
        }
    }
    else
    {
        const isize l = ms->capture[i].len;
        if (l == kCapUnfinished)
        {
            luaL_error(ms->L, "unfinished capture");
        }
        if (l == kCapPosition)
        {
            lua_pushinteger(ms->L, ms->capture[i].init - ms->src_init + 1);
        }
        else
        {
            lua_pushlstring(ms->L, ms->capture[i].init, static_cast<usize>(l));
        }
    }
}

int PushCaptures(MatchState* ms, const char* s, const char* e)
{
    const int nlevels = (ms->level == 0 && s != nullptr) ? 1 : ms->level;
    luaL_checkstack(ms->L, nlevels, "too many captures");
    for (int i = 0; i < nlevels; i++)
    {
        PushOneCapture(ms, i, s, e);
    }
    return nlevels;
}

bool NoSpecials(const char* p, usize l)
{
    for (usize i = 0; i < l; ++i)
    {
        switch (p[i])
        {
        case '^':
        case '$':
        case '*':
        case '+':
        case '?':
        case '.':
        case '(':
        case '[':
        case '%':
        case '-': return false;
        default: break;
        }
    }
    return true;
}

void Reprep(MatchState* ms, lua_State* L, const char* s, usize ls)
{
    ms->L = L;
    ms->src_init = s;
    ms->src_end = s + ls;
    ms->level = 0;
    ms->depth = 0;
    ms->steps = kMaxSteps;
}

int FindAux(lua_State* L, bool find)
{
    usize ls = 0;
    usize lp = 0;
    const char* s = luaL_checklstring(L, 1, &ls);
    const char* p = luaL_checklstring(L, 2, &lp);
    isize init = static_cast<isize>(luaL_optinteger(L, 3, 1)) - 1;
    if (init < 0)
    {
        init = 0;
    }
    else if (static_cast<usize>(init) > ls)
    {
        init = static_cast<isize>(ls);
    }
    if (find && (lua_toboolean(L, 4) != 0 || NoSpecials(p, lp)))
    {
        const char* s2 = FindPlain(L, s + init, ls - static_cast<usize>(init), p, lp);
        if (s2 != nullptr)
        {
            lua_pushinteger(L, s2 - s + 1);
            lua_pushinteger(L, static_cast<isize>(static_cast<usize>(s2 - s) + lp));
            return 2;
        }
    }
    else
    {
        MatchState ms;
        const bool anchor = (*p == '^');
        if (anchor)
        {
            p++;
        }
        const char* s1 = s + init;
        Reprep(&ms, L, s, ls);
        do
        {
            ms.level = 0;
            ms.depth = 0;
            const char* e = Match(&ms, s1, p);
            if (e != nullptr)
            {
                if (find)
                {
                    lua_pushinteger(L, s1 - s + 1);
                    lua_pushinteger(L, e - s);
                    return PushCaptures(&ms, nullptr, nullptr) + 2;
                }
                return PushCaptures(&ms, s1, e);
            }
        } while (s1++ < ms.src_end && !anchor);
    }
    lua_pushnil(L);
    return 1;
}

int StrFind(lua_State* L)
{
    return FindAux(L, true);
}

int StrMatch(lua_State* L)
{
    return FindAux(L, false);
}

int GmatchAux(lua_State* L)
{
    MatchState ms;
    usize ls = 0;
    const char* s = lua_tolstring(L, lua_upvalueindex(1), &ls);
    const char* p = lua_tostring(L, lua_upvalueindex(2));
    Reprep(&ms, L, s, ls);
    for (const char* src = s + static_cast<usize>(lua_tointeger(L, lua_upvalueindex(3))); src <= ms.src_end; src++)
    {
        ms.level = 0;
        ms.depth = 0;
        const char* e = Match(&ms, src, p);
        if (e != nullptr)
        {
            lua_Integer newstart = e - s;
            if (e == src)
            {
                newstart++;  // an empty match: advance
            }
            lua_pushinteger(L, newstart);
            lua_replace(L, lua_upvalueindex(3));
            return PushCaptures(&ms, src, e);
        }
    }
    return 0;
}

int Gmatch(lua_State* L)
{
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, &GmatchAux, 3);
    return 1;
}

void AddS(MatchState* ms, luaL_Buffer* b, const char* s, const char* e)
{
    usize l = 0;
    lua_State* L = ms->L;
    const char* news = lua_tolstring(L, 3, &l);
    for (usize i = 0; i < l; i++)
    {
        if (news[i] != kEsc)
        {
            luaL_addchar(b, news[i]);
        }
        else
        {
            i++;
            if (!IsDigit(static_cast<u8>(news[i])))
            {
                luaL_addchar(b, news[i]);
            }
            else if (news[i] == '0')
            {
                luaL_addlstring(b, s, static_cast<usize>(e - s));
            }
            else
            {
                PushOneCapture(ms, news[i] - '1', s, e);
                luaL_addvalue(b);
            }
        }
    }
}

void AddValue(MatchState* ms, luaL_Buffer* b, const char* s, const char* e)
{
    lua_State* L = ms->L;
    switch (lua_type(L, 3))
    {
    case LUA_TNUMBER:
    case LUA_TSTRING: AddS(ms, b, s, e); return;
    case LUA_TFUNCTION:
    {
        lua_pushvalue(L, 3);
        const int n = PushCaptures(ms, s, e);
        lua_call(L, n, 1);
        break;
    }
    case LUA_TTABLE:
        PushOneCapture(ms, 0, s, e);
        lua_gettable(L, 3);
        break;
    default: luaL_argerror(L, 3, "string/function/table expected"); return;
    }
    if (lua_toboolean(L, -1) == 0)
    {
        lua_pop(L, 1);
        lua_pushlstring(L, s, static_cast<usize>(e - s));
    }
    else if (lua_isstring(L, -1) == 0)
    {
        luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1));
    }
    luaL_addvalue(b);
}

int Gsub(lua_State* L)
{
    usize srcl = 0;
    const char* src = luaL_checklstring(L, 1, &srcl);
    const char* p = luaL_checkstring(L, 2);
    const int tr = lua_type(L, 3);
    const isize max_s = static_cast<isize>(luaL_optinteger(L, 4, static_cast<lua_Integer>(srcl) + 1));
    const bool anchor = (*p == '^');
    isize n = 0;
    MatchState ms;
    luaL_Buffer b;
    luaL_argcheck(L, tr == LUA_TNUMBER || tr == LUA_TSTRING || tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                  "string/function/table expected");
    luaL_buffinit(L, &b);
    if (anchor)
    {
        p++;
    }
    Reprep(&ms, L, src, srcl);
    while (n < max_s)
    {
        ms.level = 0;
        ms.depth = 0;
        const char* e = Match(&ms, src, p);
        if (e != nullptr)
        {
            n++;
            AddValue(&ms, &b, src, e);
        }
        if (e != nullptr && e > src)
        {
            src = e;
        }
        else if (src < ms.src_end)
        {
            luaL_addchar(&b, *src++);
        }
        else
        {
            break;
        }
        if (anchor)
        {
            break;
        }
    }
    luaL_addlstring(&b, src, static_cast<usize>(ms.src_end - src));
    luaL_pushresult(&b);
    lua_pushinteger(L, n);
    return 2;
}

}  // namespace

void InstallSafePatterns(lua_State* L)
{
    static const struct
    {
        const char* name;
        lua_CFunction function;
    } functions[] = {{"find", &StrFind}, {"match", &StrMatch}, {"gmatch", &Gmatch}, {"gsub", &Gsub}};
    for (const auto& f : functions)
    {
        lua_pushcfunction(L, f.function);
        lua_setfield(L, -2, f.name);
    }
}

}  // namespace engine_placeholder::script::luajit
