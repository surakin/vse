#pragma once

#include <catch2/catch_test_macros.hpp>

#include "docgen_text.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"

// File-system helpers shared by the docgen tests, over the engine's file layer (string paths, as the tests use them).
namespace docgen_test
{

namespace io = engine_placeholder::io;

inline bool ToPath(const Str& text, io::Path* out)
{
    return io::Path::Parse(text.View(), io::NativePathStyle(), out);
}

// The whole file, or "" when it cannot be read.
inline Str ReadFileText(const Str& path)
{
    io::Path p;
    engine_placeholder::Vector<engine_placeholder::u8> bytes;
    if (!ToPath(path, &p) || io::ReadFile(p, &bytes).Failed())
    {
        return Str();
    }
    return Str(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
}

// Writes `content` to `path`, creating the parent directories.
inline void WriteFileText(const Str& path, const Str& content)
{
    io::Path p;
    REQUIRE(ToPath(path, &p));
    if (!p.Parent().IsEmpty())
    {
        REQUIRE(io::CreateDirectories(p.Parent()).Ok());
    }
    REQUIRE(io::WriteFile(p, content.Data(), content.Length()).Ok());
}

// The names in `dir`, without dot entries, bytewise sorted; empty when it cannot be read.
inline StrList ListNamesSorted(const Str& dir)
{
    StrList names;
    io::Path p;
    engine_placeholder::Vector<io::DirectoryItem> items;
    if (!ToPath(dir, &p) || io::ListDirectory(p, true, &items).Failed())
    {
        return names;
    }
    for (engine_placeholder::usize i = 0; i < items.Size(); ++i)
    {
        const engine_placeholder::StringView v = items[i].name.View();
        if (v.Length() != 0 && v[0] != '.')
        {
            names.Push(v);
        }
    }
    return names;
}

inline bool IsDirectoryAt(const Str& path)
{
    io::Path p;
    return ToPath(path, &p) && io::IsDirectory(p);
}

// A new unique directory `<temp>/<prefix>.XXXXXX`.
inline Str MakeTempRoot(const char* prefix)
{
    io::Path tmp;
    io::Path made;
    REQUIRE(io::TempDirectory(&tmp).Ok());
    REQUIRE(io::CreateTempDirectory(tmp, prefix, &made).Ok());
    return Str(made.View());
}

inline void RemoveTreeAt(const Str& path)
{
    io::Path p;
    if (ToPath(path, &p))
    {
        io::RemoveAll(p);
    }
}

inline void MakeDirectory(const Str& path)
{
    io::Path p;
    if (ToPath(path, &p))
    {
        io::CreateDirectories(p);
    }
}

}  // namespace docgen_test
