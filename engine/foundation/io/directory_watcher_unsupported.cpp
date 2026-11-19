// Phase 27 Day 464: platforms without a native directory watcher yet (macOS: Day 474; Windows has had one since Day
// 473) use the polling one.
#include "io/directory_watcher.h"

namespace engine_placeholder::io
{

Status OpenNativeWatcher(const Path&, UniquePtr<DirectoryWatcher>*)
{
    return Status::Of(Error::Unsupported);
}

}  // namespace engine_placeholder::io
