#include "script_source.h"

#include "build_defs.h"
#include "algorithm.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "source_text.h"

namespace engine_placeholder::asset
{
namespace
{

void SetError(ScriptSourceError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

Status Fail(ScriptSourceError* error, u32 line, const char* message)
{
    SetError(error, line, message);
    return Status::Make(ErrorCode::Invalid, "the script is invalid");
}

Status FailName(ScriptSourceError* error, u32 line, const char* format, StringView name)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), format, static_cast<int>(name.Length()), name.Data());
    return Status::Make(ErrorCode::Invalid, "the script is invalid");
}

bool IsIdentStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c)
{
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

// A module name is dotted identifiers: `assets.player`.
bool IsModuleName(StringView name)
{
    if (name.Length() == 0 || name.Length() > kMaxScriptEntryBytes)
    {
        return false;
    }
    bool start = true;
    for (usize i = 0; i < name.Length(); ++i)
    {
        const char c = name[i];
        if (c == '.')
        {
            if (start)
            {
                return false;
            }
            start = true;
        }
        else if (start ? !IsIdentStart(c) : !IsIdentChar(c))
        {
            return false;
        }
        else
        {
            start = false;
        }
    }
    return !start;
}

// ---- the lexer ----------------------------------------------------------------------------------------------------

enum class Kind : u8
{
    Ident,
    String,
    Punct,
    Other,
};

struct Token
{
    Kind kind = Kind::Other;
    bool plain = false;  // a String that is quoted and has no escape (usable as a module name)
    usize begin = 0;
    usize end = 0;
    u32 line = 1;
};

// The length of a long bracket opener `[==[` at `at` (0 when there is none).
usize LongOpen(StringView text, usize at, usize* level)
{
    if (at >= text.Length() || text[at] != '[')
    {
        return 0;
    }
    usize i = at + 1;
    usize eq = 0;
    while (i < text.Length() && text[i] == '=')
    {
        ++eq;
        ++i;
    }
    if (i >= text.Length() || text[i] != '[')
    {
        return 0;
    }
    *level = eq;
    return i + 1 - at;
}

// The index one past the long bracket's closer, counting lines; the end of the text when it is not closed.
usize LongClose(StringView text, usize from, usize level, u32* line)
{
    usize i = from;
    while (i < text.Length())
    {
        if (text[i] == '\n')
        {
            ++*line;
        }
        else if (text[i] == ']')
        {
            usize j = i + 1;
            usize eq = 0;
            while (j < text.Length() && text[j] == '=')
            {
                ++eq;
                ++j;
            }
            if (eq == level && j < text.Length() && text[j] == ']')
            {
                return j + 1;
            }
        }
        ++i;
    }
    return text.Length();
}

void Lex(StringView text, Vector<Token>* out)
{
    usize i = 0;
    u32 line = 1;
    while (i < text.Length())
    {
        const char c = text[i];
        if (c == '\n')
        {
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
        {
            ++i;
            continue;
        }
        if (c == '-' && i + 1 < text.Length() && text[i + 1] == '-')
        {
            usize level = 0;
            const usize open = LongOpen(text, i + 2, &level);
            if (open != 0)
            {
                i = LongClose(text, i + 2 + open, level, &line);
            }
            else
            {
                while (i < text.Length() && text[i] != '\n')
                {
                    ++i;
                }
            }
            continue;
        }
        Token t;
        usize long_level = 0;
        t.begin = i;
        t.line = line;
        if (IsIdentStart(c))
        {
            while (i < text.Length() && IsIdentChar(text[i]))
            {
                ++i;
            }
            t.kind = Kind::Ident;
        }
        else if (c >= '0' && c <= '9')
        {
            while (i < text.Length() &&
                   (IsIdentChar(text[i]) || text[i] == '.' ||
                    ((text[i] == '+' || text[i] == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E'))))
            {
                ++i;
            }
            t.kind = Kind::Other;
        }
        else if (c == '"' || c == '\'')
        {
            ++i;
            bool escape = false;
            while (i < text.Length() && text[i] != c && text[i] != '\n')
            {
                if (text[i] == '\\' && i + 1 < text.Length())
                {
                    escape = true;
                    if (text[i + 1] == '\n')
                    {
                        ++line;
                    }
                    ++i;
                }
                ++i;
            }
            const bool closed = i < text.Length() && text[i] == c;
            if (closed)
            {
                ++i;
            }
            t.kind = Kind::String;
            t.plain = closed && !escape;
        }
        else if (c == '[' && LongOpen(text, i, &long_level) != 0)
        {
            const usize open = LongOpen(text, i, &long_level);
            i = LongClose(text, i + open, long_level, &line);
            t.kind = Kind::String;
            t.plain = false;
        }
        else if (c == '.' && i + 1 < text.Length() && text[i + 1] == '.')
        {
            i += (i + 2 < text.Length() && text[i + 2] == '.') ? 3 : 2;
            t.kind = Kind::Punct;
        }
        else
        {
            ++i;
            t.kind = Kind::Punct;
        }
        t.end = i;
        out->Push(t);
    }
}

bool IsPunct(StringView text, const Token& t, char c)
{
    return t.kind == Kind::Punct && t.end - t.begin == 1 && text[t.begin] == c;
}

StringView TextOf(StringView text, const Token& t)
{
    return text.Substring(t.begin, t.end - t.begin);
}

bool IsBanned(StringView name)
{
    static const char* const banned[] = {"io",      "os",      "debug",      "package",  "ffi",
                                         "jit",     "load",    "loadstring", "loadfile", "dofile",
                                         "setfenv", "getfenv", "module",     "newproxy"};
    for (const char* b : banned)
    {
        if (name == StringView(b))
        {
            return true;
        }
    }
    return false;
}

// ---- UTF-8 --------------------------------------------------------------------------------------------------------

bool ValidUtf8(StringView text)
{
    usize i = 0;
    while (i < text.Length())
    {
        const u8 c = static_cast<u8>(text[i]);
        usize extra = 0;
        u32 minimum = 0;
        u32 cp = 0;
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0)
        {
            extra = 1;
            minimum = 0x80;
            cp = c & 0x1F;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            extra = 2;
            minimum = 0x800;
            cp = c & 0x0F;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            extra = 3;
            minimum = 0x10000;
            cp = c & 0x07;
        }
        else
        {
            return false;
        }
        if (i + extra >= text.Length())
        {
            return false;
        }
        for (usize k = 1; k <= extra; ++k)
        {
            const u8 d = static_cast<u8>(text[i + k]);
            if ((d & 0xC0) != 0x80)
            {
                return false;
            }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

// ---- the validation run -------------------------------------------------------------------------------------------

// Defines `behavior` and `__require` the way a script sees them, so the source can run once without the engine and
// report the behaviors it defines.
const char* PreludeSource()
{
    return R"LUA(
local names = {}
local bad = {}
local seen = {}
local stub = {}
setmetatable(stub, { __index = function() return stub end, __call = function() return stub end })
local lifecycle = { 'on_create', 'on_enable', 'on_disable', 'on_start', 'on_fixed_update', 'on_update', 'on_destroy' }
function __require()
  return stub
end
function behavior(name, class)
  if type(name) ~= 'string' or name == '' then
    error('behavior: the name must be a non-empty string', 2)
  end
  if type(class) ~= 'table' then
    error('behavior: the second argument must be a table of callbacks', 2)
  end
  if name:find('\n', 1, true) or #name > 255 then
    error('behavior: the name has a line break or is longer than 255 bytes', 2)
  end
  if seen[name] then
    bad[#bad + 1] = "behavior '" .. name .. "' is defined twice"
  end
  seen[name] = true
  names[#names + 1] = name
  for i = 1, #lifecycle do
    local v = class[lifecycle[i]]
    if v ~= nil and type(v) ~= 'function' then
      bad[#bad + 1] = "behavior '" .. name .. "': " .. lifecycle[i] .. ' is a ' .. type(v) .. ', not a function'
    end
  end
  return class
end
function __report()
  return table.concat(names, '\n'), table.concat(bad, '\n')
end
)LUA";
}

// Copies a backend message into the error and takes the line from a `name:LINE:` prefix when there is one.
void FromOutcome(const script::Outcome& outcome, ScriptSourceError* error)
{
    StringView message = outcome.message.View();
    u32 line = 0;
    for (usize i = 0; i + 1 < message.Length(); ++i)
    {
        if (message[i] == ':' && message[i + 1] >= '0' && message[i + 1] <= '9')
        {
            usize j = i + 1;
            u32 value = 0;
            while (j < message.Length() && message[j] >= '0' && message[j] <= '9' && value < 100000000)
            {
                value = value * 10 + static_cast<u32>(message[j] - '0');
                ++j;
            }
            if (j < message.Length() && message[j] == ':')
            {
                line = value;
                // The caller names the chunk already: keep only the sentence after `name:LINE:`.
                usize start = j + 1;
                while (start < message.Length() && message[start] == ' ')
                {
                    ++start;
                }
                message = message.Substring(start, message.Length() - start);
            }
            break;
        }
    }
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%.*s", static_cast<int>(message.Length()), message.Data());
}

bool SplitLines(StringView text, Vector<String<HeapAllocator>>* out)
{
    usize pos = 0;
    while (pos <= text.Length() && text.Length() != 0)
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        out->Push(String<HeapAllocator>(HeapAllocator::Default(), text.Substring(pos, end - pos)));
        pos = end + 1;
    }
    return true;
}

}  // namespace

void SetScriptError(ScriptSourceError* error, const char* message)
{
    SetError(error, 0, message);
}

Status ParseScriptSource(StringView text, ScriptSource* out, ScriptSourceError* error)
{
    *out = ScriptSource();
    *error = ScriptSourceError();
    bool have_id = false;
    bool have_lua = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        ++line_number;
        const srctext::LineTokens t = srctext::Tokenize(text.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0 && !t.overflow)
        {
            continue;
        }
        if (t.overflow)
        {
            return Fail(error, line_number, "too many fields on the line");
        }
        const StringView kind = t.tokens[0];
        if (kind == StringView("id"))
        {
            if (have_id)
            {
                return Fail(error, line_number, "the id is given twice");
            }
            if (t.count != 2 || ParseResourceId(t.tokens[1], &out->id).Failed())
            {
                return Fail(error, line_number, "id needs one 16-digit hex resource id");
            }
            have_id = true;
            continue;
        }
        if (!have_id)
        {
            return Fail(error, line_number, "the id line must come first");
        }
        if (kind == StringView("lua"))
        {
            if (have_lua)
            {
                return Fail(error, line_number, "the lua file is given twice");
            }
            if (t.count != 2)
            {
                return Fail(error, line_number, "lua needs one path");
            }
            out->lua = String<HeapAllocator>(HeapAllocator::Default(), t.tokens[1]);
            have_lua = true;
            continue;
        }
        if (kind == StringView("dep"))
        {
            ScriptDependency dep;
            if (t.count != 3 || !IsModuleName(t.tokens[1]) || ParseResourceId(t.tokens[2], &dep.id).Failed())
            {
                return Fail(error, line_number, "dep needs a module name (dotted identifiers) and a 16-digit hex id");
            }
            dep.name = String<HeapAllocator>(HeapAllocator::Default(), t.tokens[1]);
            if (dep.id == out->id)
            {
                return Fail(error, line_number, "a script cannot depend on itself");
            }
            for (usize i = 0; i < out->dependencies.Size(); ++i)
            {
                if (out->dependencies[i].name.View() == dep.name.View())
                {
                    return Fail(error, line_number, "the dependency name is given twice");
                }
                if (out->dependencies[i].id == dep.id)
                {
                    return Fail(error, line_number, "the dependency id is given twice");
                }
            }
            out->dependencies.Push(Move(dep));
            continue;
        }
        return Fail(error, line_number, "unknown line (expected id, lua or dep)");
    }
    if (!have_id)
    {
        return Fail(error, 0, "the source has no id line");
    }
    if (!have_lua)
    {
        return Fail(error, 0, "the source has no lua line");
    }
    return Status::Success();
}

void ScanScriptText(StringView text, ScriptScan* out)
{
    *out = ScriptScan();
    Vector<Token> tokens;
    Lex(text, &tokens);
    for (usize i = 0; i < tokens.Size(); ++i)
    {
        const Token& t = tokens[i];
        if (t.kind != Kind::Ident)
        {
            continue;
        }
        const StringView name = TextOf(text, t);
        const bool field = i > 0 && (IsPunct(text, tokens[i - 1], '.') || IsPunct(text, tokens[i - 1], ':'));
        if (field)
        {
            if (name == StringView("dump") && i >= 2 && IsPunct(text, tokens[i - 1], '.') &&
                tokens[i - 2].kind == Kind::Ident && TextOf(text, tokens[i - 2]) == StringView("string") &&
                out->banned.Length() == 0)
            {
                out->banned = String<HeapAllocator>(HeapAllocator::Default(), StringView("string.dump"));
                out->banned_line = t.line;
            }
            continue;
        }
        // A table key (`{ debug = true }`) names no library; `a, load = 1, 2` assigns a global and passes this test
        // too.
        const bool key = i > 0 && i + 1 < tokens.Size() && IsPunct(text, tokens[i + 1], '=') &&
                         !(i + 2 < tokens.Size() && IsPunct(text, tokens[i + 2], '=') &&
                           tokens[i + 2].begin == tokens[i + 1].end) &&
                         (IsPunct(text, tokens[i - 1], '{') || IsPunct(text, tokens[i - 1], ',') ||
                          IsPunct(text, tokens[i - 1], ';'));
        if (IsBanned(name) && !key)
        {
            if (out->banned.Length() == 0)
            {
                out->banned = String<HeapAllocator>(HeapAllocator::Default(), name);
                out->banned_line = t.line;
            }
            continue;
        }
        if (name != StringView("require"))
        {
            continue;
        }
        usize j = i + 1;
        bool paren = false;
        if (j < tokens.Size() && IsPunct(text, tokens[j], '('))
        {
            paren = true;
            ++j;
        }
        const bool literal = j < tokens.Size() && tokens[j].kind == Kind::String && tokens[j].plain;
        if (!literal || (paren && !(j + 1 < tokens.Size() && IsPunct(text, tokens[j + 1], ')'))))
        {
            if (out->dynamic_require_line == 0)
            {
                out->dynamic_require_line = t.line;
            }
            continue;
        }
        ScriptScan::Require r;
        r.name = String<HeapAllocator>(HeapAllocator::Default(),
                                       text.Substring(tokens[j].begin + 1, tokens[j].end - tokens[j].begin - 2));
        r.begin = t.begin;
        r.end = paren ? tokens[j + 1].end : tokens[j].end;
        r.line = t.line;
        out->requires_.Push(Move(r));
        i = paren ? j + 1 : j;
    }
}

Status NormalizeScriptText(StringView text, String<HeapAllocator>* out, ScriptSourceError* error)
{
    *out = String<HeapAllocator>(HeapAllocator::Default());
    if (text.Length() >= 3 && static_cast<u8>(text[0]) == 0xEF && static_cast<u8>(text[1]) == 0xBB &&
        static_cast<u8>(text[2]) == 0xBF)
    {
        text = text.Substring(3, text.Length() - 3);
    }
    if (text.Length() > kMaxScriptSourceBytes)
    {
        return Fail(error, 0, "the script is larger than the 1 MiB limit");
    }
    u32 line = 1;
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] == '\0')
        {
            return Fail(error, line, "the script has a NUL byte");
        }
        if (text[i] == '\n')
        {
            ++line;
        }
    }
    if (!ValidUtf8(text))
    {
        return Fail(error, 0, "the script is not valid UTF-8");
    }
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] == '\r')
        {
            out->Append('\n');
            if (i + 1 < text.Length() && text[i + 1] == '\n')
            {
                ++i;
            }
        }
        else
        {
            out->Append(text[i]);
        }
    }
    return Status::Success();
}

Status MintScript(const ScriptSource& source, StringView script_text, const TypeRegistry& types,
                  const ScriptMintOptions& options, Vector<byte>* out, ScriptSourceError* error, u32 target)
{
    *error = ScriptSourceError();
    if (options.backend == nullptr)
    {
        return Fail(error, 0, "the minter has no script backend");
    }
    String<HeapAllocator> normal(HeapAllocator::Default());
    if (NormalizeScriptText(script_text, &normal, error).Failed())
    {
        return Status::Make(ErrorCode::Invalid, "the script is invalid");
    }
    ScriptScan scan;
    ScanScriptText(normal.View(), &scan);
    if (scan.banned.Length() != 0)
    {
        error->line = scan.banned_line;
        FormatTo(error->message.Data(), error->message.Size(), "'%.*s' is not available to scripts",
                 static_cast<int>(scan.banned.Length()), scan.banned.CStr());
        return Status::Make(ErrorCode::Invalid, "the script is invalid");
    }
    if (scan.dynamic_require_line != 0)
    {
        return Fail(error, scan.dynamic_require_line,
                    "require needs one literal module name, as require(\"assets.name\")");
    }
    Vector<u8> used;
    for (usize i = 0; i < source.dependencies.Size(); ++i)
    {
        used.Push(0);
    }
    // Rewrite every require to its id; line numbers stay as they were.
    String<HeapAllocator> rewritten(HeapAllocator::Default());
    usize pos = 0;
    for (usize r = 0; r < scan.requires_.Size(); ++r)
    {
        const ScriptScan::Require& req = scan.requires_[r];
        usize found = source.dependencies.Size();
        for (usize d = 0; d < source.dependencies.Size(); ++d)
        {
            if (source.dependencies[d].name.View() == req.name.View())
            {
                found = d;
                break;
            }
        }
        if (found == source.dependencies.Size())
        {
            return FailName(error, req.line, "require of '%.*s' has no dep line", req.name.View());
        }
        used[found] = 1;
        rewritten.Append(normal.View().Substring(pos, req.begin - pos));
        char hex[17];
        FormatResourceId(source.dependencies[found].id, hex);
        rewritten.Append(StringView("__require(\""));
        rewritten.Append(StringView(hex, 16));
        rewritten.Append(StringView("\")"));
        for (usize k = req.begin; k < req.end; ++k)
        {
            if (normal.View()[k] == '\n')
            {
                rewritten.Append('\n');
            }
        }
        pos = req.end;
    }
    rewritten.Append(normal.View().Substring(pos, normal.Length() - pos));
    for (usize d = 0; d < source.dependencies.Size(); ++d)
    {
        if (used[d] == 0)
        {
            return FailName(error, 0, "dependency '%.*s' is declared but never required",
                            source.dependencies[d].name.View());
        }
    }

    ScriptBuild build;
    build.language = String<HeapAllocator>(HeapAllocator::Default(), options.backend->Name());
    build.language_version = String<HeapAllocator>(HeapAllocator::Default(), options.backend->Version());
    build.source = String<HeapAllocator>(HeapAllocator::Default(), rewritten.View());
    for (usize d = 0; d < source.dependencies.Size(); ++d)
    {
        build.dependencies.Push(source.dependencies[d].id);
    }
    Sort(Span<ResourceId>(build.dependencies.Data(), build.dependencies.Size()),
         [](ResourceId a, ResourceId b) { return a < b; });

    // The syntax check, which is also the bytecode when it is asked for.
    UniquePtr<script::State> state = options.backend->CreateState(u64{64} << 20);
    if (state == nullptr)
    {
        return Fail(error, 0, "the script backend could not make a state");
    }
    const StringView chunk = source.lua.View();
    script::Outcome compiled = state->Compile(chunk, rewritten.View(), options.bytecode ? &build.bytecode : nullptr);
    if (!compiled.IsOk())
    {
        if (compiled.status == script::Status::Unsupported)
        {
            return Fail(error, 0, "the script backend cannot compile bytecode");
        }
        FromOutcome(compiled, error);
        return Status::Make(ErrorCode::Invalid, "the script is invalid");
    }
    if (options.bytecode)
    {
        const char* arch = ENGINE_ARCH_X64 ? "x86_64" : (ENGINE_ARCH_ARM64 ? "arm64" : "other");
        char key[96];
        FormatTo(key, sizeof(key), "%.*s %.*s %s", static_cast<int>(build.language.Length()), build.language.CStr(),
                 static_cast<int>(build.language_version.Length()), build.language_version.CStr(), arch);
        build.bytecode_key = String<HeapAllocator>(HeapAllocator::Default(), StringView(key, CStrLength(key)));
    }

    if (options.check_entries)
    {
        UniquePtr<script::State> run = options.backend->CreateState(u64{64} << 20);
        if (run == nullptr)
        {
            return Fail(error, 0, "the script backend could not make a state");
        }
        script::Outcome loaded = run->Load(StringView("=prelude"), StringView(PreludeSource()));
        if (loaded.IsOk())
        {
            loaded = run->Load(chunk, rewritten.View());
        }
        if (!loaded.IsOk())
        {
            FromOutcome(loaded, error);
            char prefix[200];
            FormatTo(prefix, sizeof(prefix), "running the script once failed: %s", error->message.Data());
            FormatTo(error->message.Data(), error->message.Size(), "%s", prefix);
            return Status::Make(ErrorCode::Invalid, "the script is invalid");
        }
        script::CallOutput report;
        const script::Outcome called = run->Call(StringView("__report"), Span<const script::Value>(), script::Budget(),
                                                 &report);
        if (!called.IsOk() || report.results.Size() != 2)
        {
            return Fail(error, 0, "the script's behaviors could not be listed");
        }
        const StringView bad = report.results[1].Text();
        if (bad.Length() != 0)
        {
            const usize eol = bad.Find(StringView("\n"));
            SetError(error, 0, "");
            FormatTo(error->message.Data(), error->message.Size(), "%.*s",
                     static_cast<int>(eol < bad.Length() ? eol : bad.Length()), bad.Data());
            return Status::Make(ErrorCode::Invalid, "the script is invalid");
        }
        Vector<String<HeapAllocator>> names;
        SplitLines(report.results[0].Text(), &names);
        if (names.Size() > kMaxScriptEntries)
        {
            return Fail(error, 0, "the script defines more than 256 behaviors");
        }
        for (usize i = 0; i < names.Size(); ++i)
        {
            if (names[i].Length() > kMaxScriptEntryBytes)
            {
                return Fail(error, 0, "a behavior name is longer than 255 bytes");
            }
            build.entries.Push(Move(names[i]));
        }
    }

    const Status built = BuildScriptBlob(types, source.id, build, out, target);
    if (built.Failed())
    {
        return Fail(error, 0, "the blob could not be built");
    }
    return Status::Success();
}

Status CheckScriptGraph(StringView root_name, const ScriptSource& root, ScriptLoader loader, void* context,
                        ScriptSourceError* error, Vector<ResourceId>* order)
{
    *error = ScriptSourceError();
    if (order != nullptr)
    {
        order->Clear();
    }
    struct Frame
    {
        String<HeapAllocator> name{HeapAllocator::Default()};
        ScriptSource source;
        usize next = 0;
    };
    Vector<Frame> stack;
    Vector<ResourceId> done;
    {
        Frame f;
        f.name = String<HeapAllocator>(HeapAllocator::Default(), root_name);
        f.source.id = root.id;
        f.source.lua = String<HeapAllocator>(HeapAllocator::Default(), root.lua.View());
        for (usize i = 0; i < root.dependencies.Size(); ++i)
        {
            ScriptDependency d;
            d.name = String<HeapAllocator>(HeapAllocator::Default(), root.dependencies[i].name.View());
            d.id = root.dependencies[i].id;
            f.source.dependencies.Push(Move(d));
        }
        stack.Push(Move(f));
    }
    auto chain = [&](StringView last)
    {
        String<HeapAllocator> text(HeapAllocator::Default());
        for (usize i = 0; i < stack.Size(); ++i)
        {
            text.Append(stack[i].name.View());
            text.Append(StringView(" -> "));
        }
        text.Append(last);
        return text;
    };
    while (!stack.Empty())
    {
        Frame& top = stack[stack.Size() - 1];
        if (top.next >= top.source.dependencies.Size())
        {
            if (order != nullptr)
            {
                order->Push(top.source.id);
            }
            done.Push(top.source.id);
            stack.Pop();
            continue;
        }
        const ScriptDependency& dep = top.source.dependencies[top.next++];
        bool on_stack = false;
        for (usize i = 0; i < stack.Size(); ++i)
        {
            on_stack = on_stack || stack[i].source.id == dep.id;
        }
        if (on_stack)
        {
            const String<HeapAllocator> text = chain(dep.name.View());
            error->line = 0;
            FormatTo(error->message.Data(), error->message.Size(), "a dependency cycle: %.*s",
                     static_cast<int>(text.Length()), text.CStr());
            return Status::Make(ErrorCode::Invalid, "the script graph is invalid");
        }
        bool seen = false;
        for (usize i = 0; i < done.Size(); ++i)
        {
            seen = seen || done[i] == dep.id;
        }
        if (seen)
        {
            continue;
        }
        Frame next;
        ScriptSourceError inner;
        if (!loader(context, dep.name.View(), dep.id, &next.source, &inner))
        {
            const String<HeapAllocator> text = chain(dep.name.View());
            error->line = 0;
            FormatTo(error->message.Data(), error->message.Size(), "a missing dependency: %.*s (%s)",
                     static_cast<int>(text.Length()), text.CStr(), inner.message.Data());
            return Status::Make(ErrorCode::Invalid, "the script graph is invalid");
        }
        next.name = String<HeapAllocator>(HeapAllocator::Default(), dep.name.View());
        stack.Push(Move(next));
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
