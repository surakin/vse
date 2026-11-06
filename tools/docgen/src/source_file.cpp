#include "source_file.h"

#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"

namespace docgen {

namespace io = engine_placeholder::io;

namespace {

bool ToPath(StringView text, io::Path* out) {
    return io::Path::Parse(text.Empty() ? StringView(".") : text, io::NativePathStyle(), out);
}

engine_placeholder::u32 g_name_order_seed = 0;

StringView Copy(StringView s, ArenaAllocator& arena) { return ArenaCat(arena, s, StringView()); }

} // namespace

bool LoadSourceFile(StringView path, ArenaAllocator& arena, StringView& out) {
    usize size = 0;
    engine_placeholder::u8* data = nullptr;
    io::Path file_path;
    if (!ToPath(path, &file_path)) return false;
    if (io::ReadFile(file_path, arena, &data, &size).Failed()) return false;
    out = StringView(reinterpret_cast<const char*>(data), size);
    return true;
}

EntryKind KindOf(StringView path, bool follow_links) {
    io::Path p;
    io::FileInfo info;
    if (!ToPath(path, &p) || (follow_links ? io::Stat(p, &info) : io::Lstat(p, &info)).Failed()) return EntryKind::Missing;
    switch (info.type) {
    case io::FileType::Regular: return EntryKind::File;
    case io::FileType::Directory: return EntryKind::Directory;
    default: return EntryKind::Other;
    }
}

void SetNameOrderSeedForTesting(engine_placeholder::u32 seed) { g_name_order_seed = seed; }

bool ListNames(StringView dir, ArenaAllocator& arena, Vector<StringView, ArenaAllocator>& names) {
    io::Path p;
    io::Directory d;
    if (!ToPath(dir, &p) || io::Directory::Open(p, &d).Failed()) return false;
    io::DirectoryEntry entry;
    bool found = false;
    const usize first = names.Size();
    for (;;) {
        if (d.Next(&entry, &found).Failed()) {
            while (names.Size() > first) names.Pop(); // no partial list
            return false;
        }
        if (!found) break;
        names.Push(Copy(entry.name, arena));
    }
    if (g_name_order_seed != 0) { // Fisher-Yates with a small LCG: the same seed gives the same order
        engine_placeholder::u64 state = g_name_order_seed;
        for (usize i = names.Size(); i > first + 1; --i) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const usize j = first + static_cast<usize>((state >> 33) % (i - first));
            const StringView tmp = names[i - 1];
            names[i - 1] = names[j];
            names[j] = tmp;
        }
    }
    return true;
}

bool EnsureDirectories(StringView path) {
    io::Path p;
    return !path.Empty() && ToPath(path, &p) && io::CreateDirectories(p).Ok();
}

bool WriteTextAtomic(StringView path, StringView content) {
    io::Path p;
    return !path.Empty() && ToPath(path, &p) && io::WriteFileAtomic(p, content.Data(), content.Length(), 0644).Ok(); // generated pages are ordinary files
}

bool WriteText(StringView path, StringView content) {
    io::Path p;
    return !path.Empty() && ToPath(path, &p) && io::WriteFile(p, content.Data(), content.Length()).Ok();
}

bool RemoveFile(StringView path) {
    io::Path p;
    return !path.Empty() && ToPath(path, &p) && io::Remove(p).Ok();
}

bool MakeScratchDirectory(StringView base, StringView prefix, ArenaAllocator& arena, StringView& out) {
    io::Path dir;
    io::Path made;
    if (base.Empty() ? io::TempDirectory(&dir).Failed() : !ToPath(base, &dir)) return false;
    if (io::CreateTempDirectory(dir, prefix, &made).Failed()) return false;
    out = Copy(made.View(), arena);
    return true;
}

bool RemoveTree(StringView path) {
    io::Path p;
    return !path.Empty() && ToPath(path, &p) && io::RemoveAll(p).Ok();
}

StringView CanonicalOrSame(StringView path, ArenaAllocator& arena) {
    io::Path p;
    io::Path canonical;
    if (ToPath(path, &p) && io::Canonical(p, &canonical).Ok()) return Copy(canonical.View(), arena);
    return Copy(path, arena);
}

bool WorkingDirectory(ArenaAllocator& arena, StringView& out) {
    io::Path cwd;
    if (io::CurrentDirectory(&cwd).Failed()) return false;
    out = Copy(cwd.View(), arena);
    return true;
}

} // namespace docgen
