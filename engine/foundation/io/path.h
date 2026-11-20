#pragma once

#include "containers/vector.h"
#include "string/string.h"
#include "types.h"
#include "build_defs.h"
#include "cstr/mem_ops.h"

//! Lexical path manipulation: [[io::Path]], [[io::PathBuilder]] and [[io::PathStyle]].
//!
//! Nothing here touches the disk or the operating system: a path is a value, and every operation is a function of its
//! text. Operations that need the file system (absolute, canonical, exists, symbolic links) live in the file-system
//! helpers. The rules were fixed by a table of 60 inputs per style
//! (`docs/superpowers/specs/2026-11-06-file-layer-design.md`, Appendix A) and are tested against it.
namespace vse::io
{

/// Which path grammar a [[Path]] follows. The grammar is a property of the value, so Windows rules are testable on
/// Linux.
enum class PathStyle : u8
{
    Posix,    ///< `/` separates; `\` is an ordinary name character; a root is a leading `/`.
    Windows,  ///< `/` and `\` separate; `C:` is a drive, `//server/share` a UNC root; `C:foo` is drive-relative.
};

/// Returns the style of the platform the program was built for.
/// - Backend: Windows: PathStyle::Windows; macOS, Android, iOS and consoles: PathStyle::Posix.
constexpr PathStyle NativePathStyle()
{
#if ENGINE_PLATFORM_WINDOWS
    return PathStyle::Windows;
#else
    return PathStyle::Posix;
#endif
}

/// The longest path, in bytes, a [[Path]] or [[PathBuilder]] holds. Longer text is refused, never truncated.
constexpr usize kMaxPathBytes = 4096;

/// A value type holding a UTF-8 path in canonical form: `/` as the only separator, repeated and trailing separators
/// removed (so a path never ends in a separator except a root, and no operation adds one), a `PathStyle` that decides
/// how the text is read.
///
/// Canonical form does not resolve `.` or `..` (see [[Path::Normalize]]) and never changes the case of a name. The
/// empty path is a valid value meaning "no path"; it is not the same as `.`. Equality, ordering and hashing follow the
/// style: POSIX compares bytes, Windows compares ASCII letters case-insensitively and every other byte exactly.
///
/// Memory comes from the default heap allocator; copying, `Parent`, `Normalize` and the builders allocate and abort
/// when the allocator is out of memory. For code that cannot allocate use [[PathBuilder]].
///
/// - Thread-safety: thread-compatible. Const operations are safe from many threads.
/// - Ownership: owns its text.
class Path
{
public:
    /// Creates an empty path in the native style.
    Path() = default;
    /// Creates an empty path in `style`.
    explicit Path(PathStyle style) : style_(style) {}

    /// Creates a path from literal or trusted text.
    ///
    /// - Param text: UTF-8 path text; separators of the style are accepted.
    /// - Param style: grammar to read it with.
    /// - Aborts: `text` is longer than [[io::kMaxPathBytes]] or contains a NUL byte. Use [[Path::Parse]] for text from
    /// outside.
    Path(StringView text, PathStyle style = NativePathStyle());

    /// Copies the path (allocates).
    Path(const Path& other);
    /// Replaces this path with a copy of `other` (allocates).
    Path& operator=(const Path& other);
    Path(Path&&) noexcept = default;
    Path& operator=(Path&&) noexcept = default;

    /// Builds a path from untrusted text.
    ///
    /// - Param text: UTF-8 path text.
    /// - Param style: grammar to read it with.
    /// - Param out: receives the path; unchanged on failure.
    /// - Fails: returns false when `text` is longer than [[io::kMaxPathBytes]] (after canonicalization) or contains a
    /// NUL byte.
    static bool Parse(StringView text, PathStyle style, Path* out);

    /// Returns the grammar this path follows.
    PathStyle Style() const { return style_; }
    /// Returns the canonical text, valid until the path is modified or destroyed.
    /// Not NUL-terminated: use [[Path::ToNative]] for a system call.
    StringView View() const { return StringView(text_.Size() == 0 ? "" : text_.Data(), text_.Size()); }
    /// Returns a NUL-terminated copy of the canonical text in `buffer`, with `\` separators on Windows style.
    ///
    /// - Param buffer: destination; at least `View().Length() + 1` bytes.
    /// - Param capacity: size of `buffer`.
    /// - Fails: returns false, writing nothing, when `capacity` is too small.
    bool ToNative(char* buffer, usize capacity) const;

    /// Returns true when the path has no text.
    bool IsEmpty() const { return text_.Size() == 0; }
    /// Returns true when the path names a place without reference to a current directory: a root directory on POSIX;
    /// a root directory with a drive or UNC root name on Windows (`/a` and `C:a` are not absolute there).
    bool IsAbsolute() const;
    /// Returns `!IsAbsolute()`; the empty path is relative.
    bool IsRelative() const { return !IsAbsolute(); }
    /// Returns true when the path starts with a root name or a root directory.
    bool HasRoot() const;
    /// Returns the root name (`C:`, `//server/share`; always empty on POSIX), as a view into this path.
    StringView RootName() const;

    /// Returns the path without its last component. The parent of a root (or of a root name alone) is itself, the
    /// parent of a single relative name is the empty path. Computed on the text as written: the parent of `a/..` is
    /// `a`.
    Path Parent() const;
    /// Returns the last component (`..` and `.` included), or empty for a root and the empty path.
    StringView FileName() const;
    /// Returns [[Path::FileName]] without [[Path::Extension]].
    StringView Stem() const;
    /// Returns the part of the file name from its last dot, dot included. Empty when the name has no dot after its
    /// first byte (`.gitignore`), and for `.` and `..`. `a.` has the extension `.`; `a.tar.gz` has `.gz`.
    StringView Extension() const;

    /// Iterates over the components that follow the root (names, `.` and `..`), without allocating.
    class Components
    {
    public:
        /// Writes the next component to `out`.
        ///
        /// - Returns: false when there are no more.
        bool Next(StringView* out);

    private:
        friend class Path;
        StringView rest_;
    };
    /// Returns an iterator over the components, positioned before the first.
    Components GetComponents() const;

    /// Returns `*this` joined with `right`. An absolute `right` replaces `*this`; a `right` with a different root name
    /// replaces it too; a rooted `right` without a drive keeps this path's root name (Windows); empty operands are
    /// identities. `.` and `..` are kept as written.
    ///
    /// - Aborts: the result would be longer than [[io::kMaxPathBytes]]. Use [[Path::TryJoin]] for untrusted input.
    Path Join(const Path& right) const;
    /// Like [[Path::Join(const Path&)]] with a text operand read in this path's style.
    Path Join(StringView right) const;
    /// Like [[Path::Join(const Path&)]], reporting a too-long result instead of aborting.
    ///
    /// - Aborts: the styles of the two paths differ.
    /// - Fails: returns false and leaves `out` unchanged when the result would be longer than [[io::kMaxPathBytes]].
    bool TryJoin(const Path& right, Path* out) const;
    /// Same as [[Path::Join(const Path&)]].
    Path operator/(const Path& right) const { return Join(right); }
    /// Same as [[Path::Join(StringView)]].
    Path operator/(StringView right) const { return Join(right); }
    /// Replaces `*this` with `Join(right)`.
    ///
    /// - Aborts: as [[Path::Join(StringView)]].
    void Append(StringView right);

    /// Returns the path with its last component replaced by `name` (appended when there is none).
    ///
    /// - Param name: a single component; must not contain a separator, nor a colon on Windows (`D:x` would be read as a
    /// drive).
    /// - Aborts: `name` contains a separator (or a colon on Windows), or the result is longer than
    /// [[io::kMaxPathBytes]].
    Path WithFileName(StringView name) const;
    /// Returns the path with its extension replaced. `extension` may start with a dot or not; empty removes it.
    ///
    /// - Aborts: the path has no file name (or its name is `.` or `..`), `extension` contains a separator (or a colon
    /// on Windows), or the result is longer than [[io::kMaxPathBytes]].
    Path WithExtension(StringView extension) const;
    /// Returns the path with its extension removed; a path without one is returned unchanged.
    Path WithoutExtension() const;
    /// Returns the path with `.` removed, `..` resolved against a preceding name where it can be, leading `..` kept in
    /// a relative path, `..` at a root dropped, and the empty result written `.`. Symbolic links are not considered:
    /// the answer can differ from the file system's when a link is involved.
    Path Normalize() const;
    /// Writes to `out` the relative path that, joined to `base`, names this path; both are normalized first and
    /// symbolic links are not considered.
    ///
    /// - Param base: the directory to be relative to; same style as this path.
    /// - Param out: receives the result (`.` for the same path); unchanged on failure.
    /// - Aborts: the styles differ.
    /// - Fails: returns false when the roots differ (different drive, UNC share, or one absolute and one not), when
    ///   `base` still has `..` components the path does not share, or when the result is longer than
    ///   [[io::kMaxPathBytes]].
    bool RelativeTo(const Path& base, Path* out) const;
    /// Returns true when this path is `base` or below it, lexically (both normalized, compared by component). False for
    /// differing roots.
    bool IsInside(const Path& base) const;
    /// Returns true when the first components of this path equal the components of `prefix` (no normalization; the root
    /// parts must match).
    bool StartsWith(const Path& prefix) const;

    /// Returns a hash consistent with `==` for the same style.
    usize Hash() const;
    /// Returns true when both have the same style and equal canonical text under the style's case rule.
    bool operator==(const Path& other) const { return Compare(other) == 0; }
    /// Returns true when the paths differ.
    bool operator!=(const Path& other) const { return Compare(other) != 0; }
    /// Orders by component, then by the style's case rule; paths of different styles order by style.
    bool operator<(const Path& other) const { return Compare(other) < 0; }
    /// Three-way comparison behind the operators: negative, zero or positive.
    int Compare(const Path& other) const;

private:
    bool TrySet(StringView canonical_text);
    void Set(StringView canonical_text);
    bool TryJoinText(StringView right, Path* out) const;

    Vector<char> text_;
    PathStyle style_ = NativePathStyle();
};

/// A path in a fixed buffer of `Capacity` bytes, for code that cannot allocate (early startup, signal-adjacent code).
/// It has the same canonical form and rules as [[Path]]; it exposes the operations such code needs.
///
/// `Set` and `Append` use temporary arrays of `Capacity` bytes on the stack (two for `Append`) besides the member
/// buffer, so a large capacity costs stack; pick the smallest that fits.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its buffer (inline).
template <usize Capacity> class PathBuilder
{
public:
    static_assert(Capacity >= 2 && Capacity <= kMaxPathBytes + 1,
                  "PathBuilder: capacity must hold a path and its terminator");

    /// Creates an empty builder in `style`.
    explicit PathBuilder(PathStyle style = NativePathStyle()) : style_(style) { buffer_[0] = '\0'; }

    /// Replaces the contents with `text` (canonicalized).
    ///
    /// - Fails: returns false, leaving the contents unchanged, when the canonical text does not fit or `text` has a
    /// NUL.
    bool Set(StringView text) { return SetImpl(text); }
    /// Joins `right` as [[Path::Join(const Path&)]] does.
    ///
    /// - Fails: returns false, leaving the contents unchanged, when the result does not fit.
    bool Append(StringView right) { return AppendImpl(right); }
    /// Returns the canonical, NUL-terminated text.
    const char* CStr() const { return buffer_; }
    /// Returns the canonical text.
    StringView View() const { return StringView(buffer_, length_); }
    /// Returns the grammar in use.
    PathStyle Style() const { return style_; }

private:
    bool SetImpl(StringView text);
    bool AppendImpl(StringView right);

    char buffer_[Capacity] = {};
    usize length_ = 0;
    PathStyle style_;
};

namespace detail
{
/// Canonicalizes `text` into `out` (at most `capacity` bytes, no terminator). Returns the length, or `usize(-1)` when
/// it does not fit or `text` has a NUL.
usize CanonicalizeInto(PathStyle style, StringView text, char* out, usize capacity);
/// Joins two canonical paths into `out`; same return convention.
usize JoinInto(PathStyle style, StringView left, StringView right, char* out, usize capacity);
}  // namespace detail

template <usize Capacity> bool PathBuilder<Capacity>::SetImpl(StringView text)
{
    char tmp[Capacity];
    const usize n = detail::CanonicalizeInto(style_, text, tmp, Capacity - 1);
    if (n == static_cast<usize>(-1))
    {
        return false;
    }
    MemCopy(buffer_, tmp, n);
    buffer_[n] = '\0';
    length_ = n;
    return true;
}

template <usize Capacity> bool PathBuilder<Capacity>::AppendImpl(StringView right)
{
    char canon_right[Capacity];
    const usize rn = detail::CanonicalizeInto(style_, right, canon_right, Capacity - 1);
    if (rn == static_cast<usize>(-1))
    {
        return false;
    }
    char tmp[Capacity];
    const usize n = detail::JoinInto(style_, StringView(buffer_, length_), StringView(canon_right, rn), tmp,
                                     Capacity - 1);
    if (n == static_cast<usize>(-1))
    {
        return false;
    }
    MemCopy(buffer_, tmp, n);
    buffer_[n] = '\0';
    length_ = n;
    return true;
}

}  // namespace vse::io
