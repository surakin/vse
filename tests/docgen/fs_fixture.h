#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"

// File-system helpers shared by the docgen tests, over the engine's file layer (string paths, as the tests use them).
namespace docgen_test {

namespace io = engine_placeholder::io;

inline bool ToPath(const std::string& text, io::Path* out) {
    return io::Path::Parse(engine_placeholder::StringView(text.data(), text.size()), io::NativePathStyle(), out);
}

// The whole file, or "" when it cannot be read.
inline std::string ReadFileText(const std::string& path) {
    io::Path p;
    engine_placeholder::Vector<engine_placeholder::u8> bytes;
    if (!ToPath(path, &p) || io::ReadFile(p, &bytes).Failed()) return {};
    return std::string(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
}

// Writes `content` to `path`, creating the parent directories.
inline void WriteFileText(const std::string& path, const std::string& content) {
    io::Path p;
    REQUIRE(ToPath(path, &p));
    if (!p.Parent().IsEmpty()) REQUIRE(io::CreateDirectories(p.Parent()).Ok());
    REQUIRE(io::WriteFile(p, content.data(), content.size()).Ok());
}

// The names in `dir`, without dot entries, bytewise sorted; empty when it cannot be read.
inline std::vector<std::string> ListNamesSorted(const std::string& dir) {
    std::vector<std::string> names;
    io::Path p;
    engine_placeholder::Vector<io::DirectoryItem> items;
    if (!ToPath(dir, &p) || io::ListDirectory(p, true, &items).Failed()) return names;
    for (engine_placeholder::usize i = 0; i < items.Size(); ++i) {
        const engine_placeholder::StringView v = items[i].name.View();
        if (v.Length() != 0 && v[0] != '.') names.emplace_back(v.Data(), v.Length());
    }
    return names;
}

inline bool IsDirectoryAt(const std::string& path) {
    io::Path p;
    return ToPath(path, &p) && io::IsDirectory(p);
}

// A new unique directory `<temp>/<prefix>.XXXXXX`.
inline std::string MakeTempRoot(const char* prefix) {
    io::Path tmp;
    io::Path made;
    REQUIRE(io::TempDirectory(&tmp).Ok());
    REQUIRE(io::CreateTempDirectory(tmp, prefix, &made).Ok());
    return std::string(made.View().Data(), made.View().Length());
}

inline void RemoveTreeAt(const std::string& path) {
    io::Path p;
    if (ToPath(path, &p)) io::RemoveAll(p);
}

inline void MakeDirectory(const std::string& path) {
    io::Path p;
    if (ToPath(path, &p)) io::CreateDirectories(p);
}

} // namespace docgen_test
