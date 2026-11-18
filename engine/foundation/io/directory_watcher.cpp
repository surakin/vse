// Phase 27 Day 464: DirectoryWatcher::Open chooses between the platform's native backend and the polling one.
#include "io/directory_watcher.h"

namespace engine_placeholder::io
{

Status DirectoryWatcher::Open(const Path& directory, const WatchOptions& options, UniquePtr<DirectoryWatcher>* out)
{
    if (options.poll_interval_ms < 1 || options.poll_interval_ms > 60000)
    {
        return Status::Of(Error::Invalid);
    }
    if (!options.force_polling)
    {
        const Status native = OpenNativeWatcher(directory, out);
        if (native.Ok())
        {
            return native;
        }
        if (native.Is(Error::NotFound) || native.Is(Error::NotADirectory) || native.Is(Error::PermissionDenied))
        {
            return native;  // the polling backend would fail the same way
        }
    }
    return OpenPollingWatcher(directory, options.poll_interval_ms, out);
}

}  // namespace engine_placeholder::io
