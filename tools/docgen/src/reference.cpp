#include "reference.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "source_file.h"

namespace docgen {
namespace {

StringView Persist(StringBuilder<ArenaAllocator>& sb) { return sb.View(); }

void Problem(Vector<FileDiag, ArenaAllocator>& out, StringView path, u32 line, StringView msg) {
    out.Push(FileDiag{path, Diagnostic{Severity::Error, line, msg}});
}

// A generated page is "# Title", a blank line, then the marker line — nothing else
// counts, so a hand-written page that merely quotes the marker is left alone.
bool HasMarkerLine(StringView body) {
    usize i = 0;
    while (i < body.Length() && body[i] != '\n') ++i; // title line
    if (i + 1 >= body.Length() || body[i + 1] != '\n') return false;
    const StringView marker(kGeneratedMarker);
    const usize at = i + 2;
    if (body.Length() < at + marker.Length() || std::memcmp(body.Data() + at, marker.Data(), marker.Length()) != 0) return false;
    return at + marker.Length() == body.Length() || body[at + marker.Length()] == '\n';
}

bool EndsWith(StringView s, const char* suffix) {
    usize n = std::strlen(suffix);
    return s.Length() >= n && std::memcmp(s.Data() + s.Length() - n, suffix, n) == 0;
}

// mkdir -p: creates `path` and any missing parents; true if the directory exists afterwards.
bool MakeDirs(StringView path) {
    char buf[4096];
    if (path.Length() == 0 || path.Length() >= sizeof(buf)) return false;
    std::memcpy(buf, path.Data(), path.Length());
    buf[path.Length()] = '\0';
    for (usize i = 1; i < path.Length(); ++i) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (mkdir(buf, 0777) != 0 && errno != EEXIST) return false;
        buf[i] = '/';
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return false;
    struct stat st;
    return stat(buf, &st) == 0 && S_ISDIR(st.st_mode);
}

bool IsGenerated(const ReferenceBuild& build, StringView name) {
    for (usize i = 0; i < build.files.Size(); ++i) {
        if (build.files[i].name == name) return true;
    }
    return false;
}

// Names of `*.md` files in `dir` that carry the generated marker, sorted.
void ListGeneratedPages(StringView dir, ArenaAllocator& arena, Vector<StringView, ArenaAllocator>& out) {
    DIR* d = opendir(dir.Data());
    if (d == nullptr) return;
    Vector<StringView, ArenaAllocator> names(arena);
    while (dirent* e = readdir(d)) {
        StringView name(e->d_name, std::strlen(e->d_name));
        if (!EndsWith(name, ".md")) continue;
        names.Push(ArenaCat(arena, name, StringView()));
    }
    closedir(d);
    StableSort(names, [](StringView x, StringView y) { return x.Compare(y) < 0; });
    for (usize i = 0; i < names.Size(); ++i) {
        StringView body;
        if (LoadSourceFile(ArenaCat(arena, dir, StringView("/"), names[i]), arena, body) && HasMarkerLine(body)) {
            out.Push(names[i]);
        }
    }
}

} // namespace

bool BuildReference(StringView root, StringView map_path, ArenaAllocator& arena, ReferenceBuild& build) {
    map_path = ArenaCat(arena, map_path, StringView()); // diagnostics keep this view, so own it
    StringView text;
    if (!LoadSourceFile(map_path, arena, text)) {
        Problem(build.index.diagnostics, map_path, 1, StringView("cannot read module map"));
        build.io_failure = true;
        return false;
    }
    ParseModuleMap(text, arena, build.map);
    BuildIndex(root, map_path, build.map, arena, build.index);
    if (build.index.HasErrors()) return false;

    for (usize m = 0; m < build.map.modules.Size(); ++m) {
        StringBuilder<ArenaAllocator> sb(arena);
        RenderModulePage(build.index, m, sb, arena);
        build.files.Push(OutFile{build.index.page_names[m], Persist(sb)});
    }
    {
        StringBuilder<ArenaAllocator> sb(arena);
        RenderIndexPage(build.index, sb);
        build.files.Push(OutFile{StringView("README.md"), Persist(sb)});
    }
    {
        StringBuilder<ArenaAllocator> sb(arena);
        RenderSymbolsFile(build.index, sb);
        build.files.Push(OutFile{StringView("symbols.txt"), Persist(sb)});
    }
    StableSort(build.files, [](const OutFile& x, const OutFile& y) { return x.name.Compare(y.name) < 0; });
    return true;
}

bool WriteReference(const ReferenceBuild& build, StringView out_dir, ArenaAllocator& arena, Vector<FileDiag, ArenaAllocator>& problems) {
    StringView dir = ArenaCat(arena, out_dir, StringView());
    if (!MakeDirs(dir)) {
        Problem(problems, dir, 1, StringView("cannot create output directory"));
        return false;
    }
    bool ok = true;
    for (usize i = 0; i < build.files.Size(); ++i) {
        const OutFile& f = build.files[i];
        StringView path = ArenaCat(arena, dir, StringView("/"), f.name);
        FILE* fp = std::fopen(path.Data(), "wb");
        if (fp == nullptr) {
            Problem(problems, path, 1, StringView("cannot write file"));
            ok = false;
            continue;
        }
        const bool wrote = std::fwrite(f.content.Data(), 1, f.content.Length(), fp) == f.content.Length();
        if (std::fclose(fp) != 0 || !wrote) {
            Problem(problems, path, 1, StringView("write failed"));
            ok = false;
        }
    }
    Vector<StringView, ArenaAllocator> existing(arena);
    ListGeneratedPages(dir, arena, existing);
    for (usize i = 0; i < existing.Size(); ++i) {
        if (IsGenerated(build, existing[i])) continue;
        StringView path = ArenaCat(arena, dir, StringView("/"), existing[i]);
        if (std::remove(path.Data()) != 0) {
            Problem(problems, path, 1, StringView("cannot remove stale generated page"));
            ok = false;
        }
    }
    return ok;
}

bool CheckReference(const ReferenceBuild& build, StringView out_dir, ArenaAllocator& arena, Vector<FileDiag, ArenaAllocator>& problems) {
    StringView dir = ArenaCat(arena, out_dir, StringView());
    bool ok = true;
    for (usize i = 0; i < build.files.Size(); ++i) {
        const OutFile& f = build.files[i];
        StringView path = ArenaCat(arena, dir, StringView("/"), f.name);
        StringView disk;
        if (!LoadSourceFile(path, arena, disk)) {
            Problem(problems, path, 1, StringView("generated file is missing; run `docgen reference`"));
            ok = false;
            continue;
        }
        if (disk == f.content) continue;
        // first differing line, for the summary
        u32 line = 1;
        usize k = 0;
        const usize n = disk.Length() < f.content.Length() ? disk.Length() : f.content.Length();
        while (k < n && disk[k] == f.content[k]) {
            if (disk[k] == '\n') ++line;
            ++k;
        }
        char num[16];
        int len = 0;
        for (u32 v = line; v > 0; v /= 10) num[len++] = static_cast<char>('0' + v % 10);
        char rev[16];
        for (int c = 0; c < len; ++c) rev[c] = num[len - 1 - c];
        Problem(problems, path, line,
                ArenaCat(arena, StringView("generated file is out of date (first difference at line "), StringView(rev, static_cast<usize>(len)),
                    StringView("); run `docgen reference`")));
        ok = false;
    }
    Vector<StringView, ArenaAllocator> existing(arena);
    ListGeneratedPages(dir, arena, existing);
    for (usize i = 0; i < existing.Size(); ++i) {
        if (IsGenerated(build, existing[i])) continue;
        Problem(problems, ArenaCat(arena, dir, StringView("/"), existing[i]), 1,
                StringView("generated page is no longer produced; run `docgen reference`"));
        ok = false;
    }
    return ok;
}

} // namespace docgen
