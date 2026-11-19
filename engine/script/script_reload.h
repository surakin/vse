// Hot reload of scripts (Phase 27 Day 464): a development facility. The reloader compiles each changed script in a
// scratch state first, so a broken edit leaves the running version alone, and then replaces the changed scripts, and
// the scripts that require them, in every partition at once (docs/guide/scripting.md, "Hot reload").
#pragma once

#include "asset/resource_id.h"
#include "containers/hash_map.h"
#include "containers/shared_bytes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "io/directory_watcher.h"
#include "io/path.h"
#include "script/backend.h"
#include "script/script_world.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// One script the reloader did not apply, and why.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its text.
struct ReloadProblem
{
    /// The script's key.
    String<HeapAllocator> key{HeapAllocator::Default()};
    /// The line of a syntax error in the new text; 0 when there is none.
    u32 line = 0;
    /// What was wrong, in the language's words.
    String<HeapAllocator> message{HeapAllocator::Default()};
};

/// What one [[script::ScriptReloader::Reload]] did.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its text.
struct ReloadReport
{
    /// False when hot reload is off: nothing was done.
    bool enabled = false;
    /// How many different scripts were asked for (a script given twice counts once, the last text).
    u32 requested = 0;
    /// The scripts that did not compile: their old versions keep running.
    Vector<ReloadProblem> compile_failures;
    /// The keys not applied because they were reloaded already in this tick (ask again in a later tick).
    Vector<String<HeapAllocator>> deferred;
    /// Whether the replacement was applied (false also when nothing was left to apply).
    bool applied = false;
    /// The replacement's failure when it was not applied after it compiled: a run error of the new text, etc.
    ReloadProblem run_failure;
    /// What the replacement did, when applied.
    ReloadResult result;
};

/// Applies edited scripts to a running world, one tick at a time.
///
/// - Thread-safety: thread-compatible: call it between ticks from one thread (see
/// [[script::ScriptWorld::ReloadModules]]).
/// - Ownership: borrows the world and the backend, which must outlive it.
class ScriptReloader
{
public:
    /// Makes a reloader.
    ///
    /// - Param world: the world whose scripts it replaces; not null.
    /// - Param backend: the language, to compile a new text in a scratch state; not null.
    /// - Param enabled: `script.hot_reload`; false makes [[script::ScriptReloader::Reload]] do nothing.
    /// - Ownership: borrows both pointers.
    ScriptReloader(ScriptWorld* world, Backend* backend, bool enabled);

    /// Whether reloads are applied.
    ///
    /// - Fails: never.
    bool Enabled() const { return enabled_; }

    /// Checks and applies the new texts of scripts.
    ///
    /// Each text is compiled in a scratch state; one that does not compile is reported and its old version keeps
    /// running, while the others are applied. A script already reloaded in `tick` is deferred, and a script given twice
    /// takes its last text, so an editor that saves twice does not reload twice. The applied scripts, and every script
    /// that requires one of them, run again in every partition at one point, between ticks.
    ///
    /// - Param changes: the new texts, one per script key (the hex of its resource id); borrowed for the call.
    /// - Param tick: the number of the tick about to run, for the one-reload-per-tick limit.
    /// - Param report: receives what happened; not null.
    /// - Returns: `Ok` when nothing failed after the compile check (compile failures are in the report, not here), else
    /// the failure of the replacement, with every partition left on the old version.
    /// - Ownership: borrows `changes` for the call.
    /// - Fails: as described; never aborts.
    Outcome Reload(Span<const ModuleReplacement> changes, u64 tick, ReloadReport* report);

private:
    ScriptWorld* world_;
    Backend* backend_;
    bool enabled_;
    HashMap<String<HeapAllocator>, u64, HeapAllocator> last_tick_;  // module key -> the tick it was reloaded in
};

/// What a poll of the development directory found for one script.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: shares the bytes.
struct DevChange
{
    /// The script's resource id.
    asset::ResourceId id;
    /// The new text.
    SharedBytesPtr text;
    /// The content hash, to hand back to [[script::ScriptDevWatcher::Applied]].
    u64 hash = 0;
};

/// Watches the files of a development directory (`script.dev_directory`): the text of script `<hex id>` is the file
/// `<hex id>.lua` (the minter writes it with `asset_mint mint-script --dev-out=DIR`: the text as it is stored, with its
/// `require` calls already rewritten). It listens through [[io::DirectoryWatcher]] (inotify where there is one, polling
/// elsewhere), reads only the files an event names (every one after an `Overflow`) and compares content hashes, so a
/// save that did not change the text, or an editor that saves twice, reports once. It has no thread.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the list of watched scripts.
class ScriptDevWatcher
{
public:
    /// Makes a watcher over `directory`; an empty directory (the shipped configuration) watches nothing.
    ///
    /// - Param directory: the directory; copied.
    explicit ScriptDevWatcher(StringView directory);

    /// Whether a directory is set.
    ///
    /// - Fails: never.
    bool Active() const { return active_; }

    /// Starts watching script `id`; its file as it is now is the baseline (no change is reported for it).
    ///
    /// - Param id: the script's resource id.
    /// - Fails: never; a missing file has no baseline and is reported when it appears.
    void Watch(asset::ResourceId id);

    /// Finds the watched scripts whose file differs from the last version seen or applied. It reads a file only when
    /// the directory watcher named it (all of them on the first call and after an overflow).
    ///
    /// - Param out: receives the changes, in the order the scripts were watched; replaced.
    /// - Ownership: the changes share the text buffers they carry.
    /// - Fails: never; a file that cannot be read is skipped until the next poll.
    void Poll(Vector<DevChange>* out);

    /// Records that `change` was handled, so it is not reported again: call it for a change that was applied and for
    /// one that failed to compile (the report has said why; the next edit is a new change). A change that was deferred
    /// is not recorded: every poll reports it again until it is.
    ///
    /// - Param change: a change from [[script::ScriptDevWatcher::Poll]].
    /// - Fails: never.
    void Applied(const DevChange& change);

private:
    struct Watched
    {
        asset::ResourceId id;
        u64 hash = 0;
        bool known = false;
        bool dirty = false;  // an event named the file
    };

    bool active_ = false;
    bool rescan_ = true;  // read every file at the next poll (no watcher, or events were lost)
    UniquePtr<io::DirectoryWatcher> events_;
    io::Path directory_;
    Vector<Watched> watched_;
};

}  // namespace engine_placeholder::script
