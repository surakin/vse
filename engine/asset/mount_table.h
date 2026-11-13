#pragma once

//! [[asset::MountTable]]: the ordered set of backends the resource manager reads from. A lookup by id goes to the
//! highest-priority backend that has the resource (so a loose-file mount overrides a pak), and the mounts' id <-> path
//! tables are read asynchronously and merged.

#include "asset/backend.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "string/string.h"
#include "sync/mutex.h"

namespace engine_placeholder::asset
{

/// Names one mount ([[asset::MountTable::Mount]]).
using MountId = u32;

/// Ordered mounts and the merged table.
///
/// A mount has a path prefix (`/` for none; the entries of its table appear under it), a priority (higher wins; the
/// later of two equal priorities wins) and a backend the caller owns. Loads by id never use a table. Path questions
/// (`Resolve`, `PathsOf`) are answered from the mounts' tables in priority order: a mount whose table has not arrived
/// answers `NotReady` (never `NotFound`, which would be a lie), a mount whose table read failed answers with that
/// failure, and a path two mounts map to different ids is won by the higher priority and listed as shadowed in the
/// merged table.
/// - Thread-safety: thread-safe. The tables-ready callback runs on the thread that finished the last table read. A
///   backend must not be unmounted (or destroyed) while a call into it from `Stat` or `Open` is running.
/// - Ownership: borrows the backends (they must outlive their mount); owns its tickets and tables.
class MountTable
{
public:
    MountTable() = default;
    MountTable(const MountTable&) = delete;
    MountTable& operator=(const MountTable&) = delete;
    /// Cancels the table reads and waits for them; the backends are not touched.
    ~MountTable();

    /// Adds a mount; its table is not read until `RefreshTablesAsync` or `RefreshTables`.
    ///
    /// - Fails: `Invalid` (a null backend, a prefix that is not a valid path or `/`).
    /// - Ownership: borrows `backend`, which must outlive the mount; `*out` is written on success only.
    Status Mount(StringView prefix, ResourceBackend* backend, i32 priority, MountId* out);

    /// Removes a mount, cancelling its table read. Open readers of the backend are not affected.
    ///
    /// - Fails: `NotFound`.
    Status Unmount(MountId id);

    /// Describes `id` from the highest-priority backend that has it.
    ///
    /// - Fails: `NotFound` when no mount has it; any other failure of a backend that has it is returned as it is (a
    ///   corrupt resource is not silently replaced by a lower mount's copy).
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Stat(ResourceId id, ResourceInfo* out);

    /// Opens `id` from the highest-priority backend that has it; `*mount` (when not null) says which.
    ///
    /// - Ownership: `*out` receives the reader on success; the caller owns it.
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out, MountId* mount = nullptr);

    /// Starts every mount's `ReadTableAsync` without blocking; each table is merged as it arrives. A read still running
    /// from an earlier call is cancelled and replaced.
    void RefreshTablesAsync();

    /// Reads every mount's table and waits for them all.
    ///
    /// - Fails: the first failure, in priority order.
    Status RefreshTables();

    /// True when every mount's table has arrived (or failed) and none is being read.
    ///
    /// - Fails: never.
    bool TablesReady() const;

    /// Registers a function called once each time a refresh has finished for every mount.
    ///
    /// - Ownership: stores `function` and `user` until replaced; `user` must stay valid until then.
    void SetTablesReadyCallback(void (*function)(void* user), void* user);

    /// The id `path` names.
    ///
    /// - Fails: `NotReady` when a mount that could answer has no table yet; `NotFound` when every mount's table is in
    ///   and none has it; `Invalid` for a malformed path; a failed table read's status for its mount.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Resolve(StringView path, ResourceId* out) const;

    /// The paths of `id`, under every ready mount that has it.
    ///
    /// - Fails: `NotReady` when any mount's table has not arrived.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status PathsOf(ResourceId id, Vector<String<HeapAllocator>>* out) const;

    /// The merged table (priority order applied) and the entries it shadowed; for tests and tools.
    ///
    /// - Fails: `NotReady` when a table has not arrived.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Snapshot(ResourceTable* merged, Vector<ShadowedEntry>* shadowed) const;

    /// Polls every mount's `Watch`; the table of a mount that changed is read again asynchronously.
    ///
    /// - Param changes: all records reported, in mount order.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status PollChanges(Vector<ChangeRecord>* changes);

    /// The number of mounts.
    usize MountCount() const;

private:
    struct Slot
    {
        Slot() : prefix(HeapAllocator::Default()) {}
        MountId id = 0;
        String<HeapAllocator> prefix;
        ResourceBackend* backend = nullptr;
        i32 priority = 0;
        u32 sequence = 0;
        UniquePtr<ResourceTable> table;  // with the prefix applied; null until it arrives
        Status table_status;
        UniquePtr<TableTicket>
            ticket;            // the latest read (kept after it was merged: it cannot be destroyed in its callback)
        bool pending = false;  // a read was started and has not been merged yet
        MountTable* owner = nullptr;
    };

    static void OnTicketDone(void* slot);
    void StartLocked(Slot* slot, Vector<UniquePtr<TableTicket>>* retired);
    void TakeFinishedLocked(Slot* slot);
    static bool CouldHold(const Slot& slot, StringView path);
    static bool SlotReady(const Slot& slot) { return slot.table || slot.table_status.Failed(); }
    bool AllReadyLocked() const;

    mutable Mutex mutex_;
    Vector<UniquePtr<Slot>> slots_;  // ordered: higher priority first; of equal priorities the later mount first
    MountId next_id_ = 1;
    u32 next_sequence_ = 0;
    void (*ready_callback_)(void*) = nullptr;
    void* ready_user_ = nullptr;
};

}  // namespace engine_placeholder::asset
