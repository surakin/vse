#include "debug/crash_handler.h"

#include "algorithm.h"
#include "containers/vector.h"
#include "io/directory.h"
#include "io/file_system.h"

namespace engine_placeholder::debug {

u32 PruneSavedDumps(const io::Path& saved, u32 keep) {
    if (keep == 0) return 0;
    Vector<io::DirectoryItem> items;
    if (io::ListDirectory(saved, true, &items).Failed()) return 0;
    Vector<io::Path> paths;
    Vector<i64> times;
    for (usize i = 0; i < items.Size(); ++i) {
        if (items[i].type != io::FileType::Regular || !items[i].name.View().EndsWith(".envelope")) continue;
        paths.Push(saved.Join(items[i].name.View()));
        io::FileInfo info;
        if (io::Lstat(paths[paths.Size() - 1], &info).Failed()) { // unreadable now: leave it alone rather than guess its age
            paths.Pop();
            continue;
        }
        times.Push(info.modified_unix_ns);
    }
    if (paths.Size() <= keep) return 0;
    Vector<usize> order; // oldest first; equal times keep the (sorted) name order
    for (usize i = 0; i < paths.Size(); ++i) order.Push(i);
    Sort(Span<usize>(order.Data(), order.Size()), [&](usize a, usize b) { return times[a] != times[b] ? times[a] < times[b] : a < b; });
    u32 removed = 0;
    for (usize i = 0; i + keep < order.Size(); ++i) {
        if (io::Remove(paths[order[i]]).Ok()) ++removed;
    }
    return removed;
}

} // namespace engine_placeholder::debug
