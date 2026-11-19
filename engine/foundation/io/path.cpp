#include "io/path.h"
#include "containers/hash.h"

#include "log/assert.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::io
{

namespace
{

constexpr usize kFail = static_cast<usize>(-1);

bool IsSep(PathStyle style, char c)
{
    return c == '/' || (style == PathStyle::Windows && c == '\\');
}

/// Index of the first separator of `style` in `s`, or `s.Length()` when there is none.
usize FindSep(PathStyle style, StringView s)
{
    const usize slash = s.IndexOf('/');
    if (style == PathStyle::Posix)
    {
        return slash;
    }
    const usize back = s.IndexOf('\\');
    return slash < back ? slash : back;
}

char Fold(PathStyle style, char c)
{
    if (style == PathStyle::Windows && c >= 'A' && c <= 'Z')
    {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

int CompareFoldSlow(PathStyle style, StringView a, StringView b)
{
    const usize n = a.Length() < b.Length() ? a.Length() : b.Length();
    for (usize i = 0; i < n; ++i)
    {
        const byte x = static_cast<byte>(Fold(style, a[i]));
        const byte y = static_cast<byte>(Fold(style, b[i]));
        if (x != y)
        {
            return x < y ? -1 : 1;
        }
    }
    return a.Length() == b.Length() ? 0 : (a.Length() < b.Length() ? -1 : 1);
}

// POSIX compares bytes. Windows folds ASCII letters; equal bytes are the fast path for both.
bool EqualFold(PathStyle style, StringView a, StringView b)
{
    if (a == b)
    {
        return true;
    }
    if (style == PathStyle::Posix || a.Length() != b.Length())
    {
        return false;
    }
    return CompareFoldSlow(style, a, b) == 0;
}

int CompareFold(PathStyle style, StringView a, StringView b)
{
    if (a == b)
    {
        return 0;
    }
    return style == PathStyle::Posix ? a.Compare(b) : CompareFoldSlow(style, a, b);
}

/// The pieces of a path: root name (raw text), whether a root directory follows it, and the rest.
struct Parsed
{
    StringView root_name;
    bool root_dir = false;
    StringView rest;
};

Parsed ParseText(PathStyle style, StringView s)
{
    Parsed p;
    usize root_name_length = 0;
    if (style == PathStyle::Windows)
    {
        // UNC root: two separators, a server, a separator, a share.
        if (s.Length() >= 2 && IsSep(style, s[0]) && IsSep(style, s[1]))
        {
            const StringView after = s.Substring(2);
            const usize server = FindSep(style, after);
            if (server > 0 && server < after.Length())
            {
                const usize share = FindSep(style, after.Substring(server + 1));
                if (share > 0)
                {
                    root_name_length = 2 + server + 1 + share;
                    p.root_dir = true;  // a UNC root always has its root directory
                }
            }
        }
        // Drive letter: `C:`.
        if (root_name_length == 0 && s.Length() >= 2 && s[1] == ':' &&
            ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')))
        {
            root_name_length = 2;
        }
    }
    p.root_name = s.Substring(0, root_name_length);
    p.rest = s.Substring(root_name_length);
    if (!p.rest.Empty() && IsSep(style, p.rest[0]))
    {
        p.root_dir = true;
    }
    return p;
}

bool NextComponent(PathStyle style, StringView* rest, StringView* out)
{
    while (!rest->Empty() && IsSep(style, (*rest)[0]))
    {
        *rest = rest->Substring(1);
    }
    if (rest->Empty())
    {
        return false;
    }
    const usize end = FindSep(style, *rest);
    *out = rest->Substring(0, end);
    *rest = rest->Substring(end);
    return true;
}

bool IsAbsoluteParsed(PathStyle style, const Parsed& p)
{
    return p.root_dir && (style == PathStyle::Posix || !p.root_name.Empty());
}

/// A bounded output buffer that appends views; it records overflow instead of truncating silently.
struct Out
{
    char* buf;
    usize cap;
    usize len = 0;
    bool ok = true;
    void Put(char c) { PutView(StringView(&c, 1)); }
    void PutView(StringView v)
    {
        if (v.Length() > cap - len)
        {
            ok = false;
            return;
        }
        if (!v.Empty())
        {
            MemCopy(buf + len, v.Data(), v.Length());
            len += v.Length();
        }
    }
    /// Appends `v` with every separator of `style` written as `/`.
    void PutSlashed(PathStyle style, StringView v)
    {
        for (;;)
        {
            const usize i = FindSep(style, v);
            PutView(v.Substring(0, i));
            if (i == v.Length())
            {
                return;
            }
            Put('/');
            v = v.Substring(i + 1);
        }
    }
    void PutRoot(PathStyle style, const Parsed& p)
    {
        PutSlashed(style, p.root_name);
        if (p.root_dir)
        {
            Put('/');
        }
    }
    void PutComponent(StringView c, usize base)
    {
        if (len > base)
        {
            Put('/');
        }
        PutView(c);
    }
    usize Result() const { return ok ? len : kFail; }
};

usize RootLength(PathStyle style, StringView canonical)
{
    const Parsed p = ParseText(style, canonical);
    return p.root_name.Length() + (p.root_dir ? 1 : 0);
}

usize NormalizeInto(PathStyle style, StringView canonical, char* buf, usize cap)
{
    Out out{buf, cap};
    const Parsed p = ParseText(style, canonical);
    out.PutRoot(style, p);
    const usize base = out.len;
    StringView rest = p.rest;
    StringView c;
    while (NextComponent(style, &rest, &c))
    {
        if (c == StringView("."))
        {
            continue;
        }
        if (c == StringView(".."))
        {
            const StringView names(out.buf + base, out.len - base);
            if (!names.Empty())
            {
                const usize slash = names.LastIndexOf('/');
                const bool single = slash == names.Length();
                if (names.Substring(single ? 0 : slash + 1) != StringView(".."))
                {
                    out.len = base + (single ? 0 : slash);  // drop the last name
                    continue;
                }
            }
            else if (p.root_dir)
            {
                continue;  // never above a root
            }
        }
        out.PutComponent(c, base);
    }
    if (out.len == 0)
    {
        out.Put('.');
    }
    return out.Result();
}

}  // namespace

namespace detail
{

usize CanonicalizeInto(PathStyle style, StringView text, char* buf, usize cap)
{
    if (text.IndexOf('\0') != text.Length())
    {
        return kFail;
    }
    Out out{buf, cap};
    const Parsed p = ParseText(style, text);
    out.PutRoot(style, p);
    const usize base = out.len;
    StringView rest = p.rest;
    StringView c;
    while (NextComponent(style, &rest, &c))
    {
        out.PutComponent(c, base);
    }
    return out.Result();
}

usize JoinInto(PathStyle style, StringView left, StringView right, char* buf, usize cap)
{
    Out out{buf, cap};
    auto copy = [&](StringView s) { out.PutView(s); };
    if (right.Empty())
    {
        copy(left);
        return out.Result();
    }
    if (left.Empty())
    {
        copy(right);
        return out.Result();
    }
    const Parsed pa = ParseText(style, left);
    const Parsed pb = ParseText(style, right);
    if ((!pb.root_name.Empty() && !EqualFold(style, pb.root_name, pa.root_name)) || IsAbsoluteParsed(style, pb))
    {
        copy(right);  // a different root name, or an absolute right operand, replaces everything
        return out.Result();
    }
    StringView c;
    if (pb.root_dir)
    {  // rooted without a drive (Windows): keeps the left root name
        Parsed root = pa;
        root.root_dir = true;
        out.PutRoot(style, root);
        const usize base = out.len;
        StringView rest = pb.rest;
        while (NextComponent(style, &rest, &c))
        {
            out.PutComponent(c, base);
        }
        return out.Result();
    }
    out.PutRoot(style, pa);
    const usize base = out.len;
    StringView rest = pa.rest;
    while (NextComponent(style, &rest, &c))
    {
        out.PutComponent(c, base);
    }
    rest = pb.rest;
    while (NextComponent(style, &rest, &c))
    {
        out.PutComponent(c, base);
    }
    return out.Result();
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------------------------------

bool Path::TrySet(StringView canonical_text)
{
    text_.Clear();
    if (!text_.TryResizeUninitialized(canonical_text.Length()))
    {
        return false;
    }
    if (!canonical_text.Empty())
    {
        MemCopy(text_.Data(), canonical_text.Data(), canonical_text.Length());
    }
    return true;
}

void Path::Set(StringView canonical_text)
{
    ENGINE_ASSERT(TrySet(canonical_text), "Path: out of memory");
}

bool Path::Parse(StringView text, PathStyle style, Path* out)
{
    char tmp[kMaxPathBytes];
    const usize n = detail::CanonicalizeInto(style, text, tmp, kMaxPathBytes);
    if (n == kFail)
    {
        return false;
    }
    Path result(style);
    if (!result.TrySet(StringView(tmp, n)))
    {
        return false;
    }
    *out = static_cast<Path&&>(result);
    return true;
}

Path::Path(StringView text, PathStyle style) : style_(style)
{
    Path parsed(style);
    ENGINE_ASSERT(Parse(text, style, &parsed), "Path: text is longer than kMaxPathBytes or contains a NUL byte");
    *this = static_cast<Path&&>(parsed);
}

Path::Path(const Path& other) : style_(other.style_)
{
    Set(other.View());
}

Path& Path::operator=(const Path& other)
{
    if (this != &other)
    {
        style_ = other.style_;
        Set(other.View());
    }
    return *this;
}

bool Path::ToNative(char* buffer, usize capacity) const
{
    const usize n = text_.Size();
    if (capacity < n + 1)
    {
        return false;
    }
    if (n != 0)
    {
        MemCopy(buffer, text_.Data(), n);
    }
    buffer[n] = '\0';
    if (style_ == PathStyle::Windows)
    {
        for (usize pos = 0; pos < n;)
        {  // each separator found by search, not by testing every byte
            pos += StringView(buffer + pos, n - pos).IndexOf('/');
            if (pos < n)
            {
                buffer[pos++] = '\\';
            }
        }
    }
    return true;
}

bool Path::IsAbsolute() const
{
    return IsAbsoluteParsed(style_, ParseText(style_, View()));
}

bool Path::HasRoot() const
{
    const Parsed p = ParseText(style_, View());
    return !p.root_name.Empty() || p.root_dir;
}

StringView Path::RootName() const
{
    return ParseText(style_, View()).root_name;
}

Path Path::Parent() const
{
    const StringView t = View();
    const usize root = RootLength(style_, t);
    const StringView names = t.Substring(root);
    const usize slash = names.LastIndexOf('/');
    Path result(style_);
    // Without a separator among the names there is one name (parent: the root part) or none (a root is its own parent).
    result.Set(names.Empty() ? t : t.Substring(0, root + (slash == names.Length() ? 0 : slash)));
    return result;
}

StringView Path::FileName() const
{
    const StringView names = View().Substring(RootLength(style_, View()));
    const usize slash = names.LastIndexOf('/');
    return slash == names.Length() ? names : names.Substring(slash + 1);
}

namespace
{
// Index where the extension starts in a file name, or its length when there is none. The dot of `.gitignore` (index 0)
// starts no extension; `.` and `..` have none.
usize ExtensionStart(StringView name)
{
    if (name == StringView(".") || name == StringView(".."))
    {
        return name.Length();
    }
    const usize dot = name.LastIndexOf('.');
    return dot == 0 ? name.Length() : dot;
}
}  // namespace

StringView Path::Stem() const
{
    const StringView n = FileName();
    return n.Substring(0, ExtensionStart(n));
}

StringView Path::Extension() const
{
    const StringView n = FileName();
    return n.Substring(ExtensionStart(n));
}

bool Path::Components::Next(StringView* out)
{
    return NextComponent(PathStyle::Posix, &rest_, out);
}

Path::Components Path::GetComponents() const
{
    Components c;
    const StringView t = View();
    c.rest_ = t.Substring(RootLength(style_, t));
    return c;
}

bool Path::TryJoin(const Path& right, Path* out) const
{
    ENGINE_ASSERT(right.style_ == style_, "Path::Join: the paths have different styles");
    char tmp[kMaxPathBytes];
    const usize n = detail::JoinInto(style_, View(), right.View(), tmp, kMaxPathBytes);
    if (n == kFail)
    {
        return false;
    }
    Path result(style_);
    if (!result.TrySet(StringView(tmp, n)))
    {
        return false;
    }
    *out = static_cast<Path&&>(result);
    return true;
}

Path Path::Join(const Path& right) const
{
    Path result(style_);
    ENGINE_ASSERT(TryJoin(right, &result), "Path::Join: the result is longer than kMaxPathBytes");
    return result;
}

bool Path::TryJoinText(StringView right, Path* out) const
{
    char canon[kMaxPathBytes];
    const usize rn = detail::CanonicalizeInto(style_, right, canon, kMaxPathBytes);
    if (rn == kFail)
    {
        return false;
    }
    char tmp[kMaxPathBytes];
    const usize n = detail::JoinInto(style_, View(), StringView(canon, rn), tmp, kMaxPathBytes);
    if (n == kFail)
    {
        return false;
    }
    Path result(style_);
    if (!result.TrySet(StringView(tmp, n)))
    {
        return false;
    }
    *out = static_cast<Path&&>(result);
    return true;
}

Path Path::Join(StringView right) const
{
    Path result(style_);
    ENGINE_ASSERT(TryJoinText(right, &result), "Path::Join: the operand or the result is longer than kMaxPathBytes");
    return result;
}

void Path::Append(StringView right)
{
    Path result(style_);
    ENGINE_ASSERT(TryJoinText(right, &result), "Path::Append: the operand or the result is longer than kMaxPathBytes");
    *this = static_cast<Path&&>(result);
}

Path Path::WithFileName(StringView name) const
{
    ENGINE_ASSERT(FindSep(style_, name) == name.Length(), "Path::WithFileName: the name contains a separator");
    ENGINE_ASSERT(style_ == PathStyle::Posix || name.IndexOf(':') == name.Length(),
                  "Path::WithFileName: the name contains a colon (a drive on Windows)");
    if (FileName().Empty())
    {
        return Join(name);
    }
    return Parent().Join(name);
}

Path Path::WithExtension(StringView extension) const
{
    const StringView name = FileName();
    ENGINE_ASSERT(!name.Empty() && name != StringView(".") && name != StringView(".."),
                  "Path::WithExtension: the path has no file name to give an extension");
    char tmp[kMaxPathBytes];
    Out out{tmp, kMaxPathBytes};
    out.PutView(Stem());
    if (!extension.Empty())
    {
        if (extension[0] != '.')
        {
            out.Put('.');
        }
        out.PutView(extension);
    }
    ENGINE_ASSERT(out.ok, "Path::WithExtension: the result is longer than kMaxPathBytes");
    return WithFileName(StringView(tmp, out.len));
}

Path Path::WithoutExtension() const
{
    if (Extension().Empty())
    {
        return *this;
    }
    return WithFileName(Stem());
}

Path Path::Normalize() const
{
    char tmp[kMaxPathBytes + 1];
    const usize n = NormalizeInto(style_, View(), tmp, kMaxPathBytes + 1);
    ENGINE_ASSERT(n != kFail,
                  "Path::Normalize: internal buffer too small");  // normalizing never lengthens a path by more than "."
    Path result(style_);
    result.Set(StringView(tmp, n));
    return result;
}

namespace
{
// A normalized path equal to "." is treated as having no components.
Parsed ParseNormalized(PathStyle style, StringView n)
{
    if (n.Length() == 1 && n[0] == '.')
    {
        return Parsed{};
    }
    return ParseText(style, n);
}
}  // namespace

bool Path::RelativeTo(const Path& base, Path* out) const
{
    ENGINE_ASSERT(base.style_ == style_, "Path::RelativeTo: the paths have different styles");
    char np_buf[kMaxPathBytes + 1];
    char nb_buf[kMaxPathBytes + 1];
    const usize np_len = NormalizeInto(style_, View(), np_buf, kMaxPathBytes + 1);
    const usize nb_len = NormalizeInto(style_, base.View(), nb_buf, kMaxPathBytes + 1);
    ENGINE_ASSERT(np_len != kFail && nb_len != kFail, "Path::RelativeTo: internal buffer too small");
    const Parsed pp = ParseNormalized(style_, StringView(np_buf, np_len));
    const Parsed pb = ParseNormalized(style_, StringView(nb_buf, nb_len));
    if (!EqualFold(style_, pp.root_name, pb.root_name) || pp.root_dir != pb.root_dir)
    {
        return false;
    }
    StringView rp = pp.rest;
    StringView rb = pb.rest;
    StringView cp;
    StringView cb;
    // Skip the common prefix.
    for (;;)
    {
        StringView save_p = rp;
        StringView save_b = rb;
        const bool has_p = NextComponent(style_, &rp, &cp);
        const bool has_b = NextComponent(style_, &rb, &cb);
        if (has_p && has_b && EqualFold(style_, cp, cb))
        {
            continue;
        }
        rp = save_p;
        rb = save_b;
        break;
    }
    char tmp[kMaxPathBytes];
    Out o{tmp, kMaxPathBytes};
    while (NextComponent(style_, &rb, &cb))
    {
        if (cb == StringView(".."))
        {
            return false;  // the base climbs somewhere the path does not
        }
        o.PutComponent(StringView(".."), 0);
    }
    while (NextComponent(style_, &rp, &cp))
    {
        o.PutComponent(cp, 0);
    }
    if (o.len == 0)
    {
        o.Put('.');
    }
    if (!o.ok)
    {
        return false;
    }
    Path result(style_);
    if (!result.TrySet(StringView(tmp, o.len)))
    {
        return false;
    }
    *out = static_cast<Path&&>(result);
    return true;
}

bool Path::IsInside(const Path& base) const
{
    Path rel(style_);
    if (!RelativeTo(base, &rel))
    {
        return false;
    }
    StringView first;
    Components c = rel.GetComponents();
    if (!c.Next(&first))
    {
        return true;
    }
    return first != StringView("..");
}

bool Path::StartsWith(const Path& prefix) const
{
    if (style_ != prefix.style_)
    {
        return false;
    }
    const Parsed a = ParseText(style_, View());
    const Parsed b = ParseText(style_, prefix.View());
    if (!EqualFold(style_, a.root_name, b.root_name) || a.root_dir != b.root_dir)
    {
        return false;
    }
    StringView ra = a.rest;
    StringView rb = b.rest;
    StringView ca;
    StringView cb;
    while (NextComponent(style_, &rb, &cb))
    {
        if (!NextComponent(style_, &ra, &ca) || !EqualFold(style_, ca, cb))
        {
            return false;
        }
    }
    return true;
}

usize Path::Hash() const
{
    Fnv1a64Builder h(kFnv1a64Basis ^ static_cast<u64>(style_));
    for (usize i = 0; i < text_.Size(); ++i)
    {
        h.AddByte(static_cast<byte>(Fold(style_, text_.Data()[i])));
    }
    return static_cast<usize>(h.Value());
}

int Path::Compare(const Path& other) const
{
    if (style_ != other.style_)
    {
        return style_ < other.style_ ? -1 : 1;
    }
    const Parsed a = ParseText(style_, View());
    const Parsed b = ParseText(style_, other.View());
    if (const int r = CompareFold(style_, a.root_name, b.root_name))
    {
        return r;
    }
    if (a.root_dir != b.root_dir)
    {
        return a.root_dir ? 1 : -1;
    }
    StringView ra = a.rest;
    StringView rb = b.rest;
    StringView ca;
    StringView cb;
    for (;;)
    {
        const bool has_a = NextComponent(style_, &ra, &ca);
        const bool has_b = NextComponent(style_, &rb, &cb);
        if (!has_a || !has_b)
        {
            return has_a == has_b ? 0 : (has_a ? 1 : -1);
        }
        if (const int r = CompareFold(style_, ca, cb))
        {
            return r;
        }
    }
}

}  // namespace engine_placeholder::io
