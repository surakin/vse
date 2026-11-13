#pragma once

//! [[asset::ResourceBackend]]: the seam every source of resources implements (loose files, the memory backend of a
//! host application, later paks and the network), with [[asset::ResourceReader]] (ranged reads of one resource) and
//! [[asset::TableTicket]] (the asynchronous read of a backend's id <-> path table).

#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_table.h"
#include "asset/status.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/file.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// What a backend can do ([[asset::ResourceBackend::Capabilities]] is a mask of these).
enum BackendCapability : u32
{
    kBackendRanged = 1,        ///< `ReadAt` reads any range without reading the rest.
    kBackendAsync = 2,         ///< `ReadAtAsync` completes on its own, without a thread of the caller's.
    kBackendWatch = 4,         ///< `Watch` reports changes.
    kBackendWritable = 8,      ///< The backend accepts changes from its host (the memory backend).
    kBackendStoredTable = 16,  ///< The backend has a stored id <-> path table (a special file, a pak section, a reply).
    kBackendAsyncTable = 32,   ///< `ReadTableAsync` reads natively, without a thread (the network backend).
};

/// What a backend knows about one resource without reading it.
///
/// - Thread-safety: immutable value.
struct ResourceInfo
{
    u64 size = 0;           ///< The size of the blob in bytes.
    ContentHash hash;       ///< The blob's content hash when `has_hash` is true.
    bool has_hash = false;  ///< True when the backend knows the hash without reading the blob.
    i64 modified_ns = 0;    ///< A modification stamp (nanoseconds since 1970 for files; a counter elsewhere); 0 if
                            ///< unknown.
    ResourceType type;      ///< The resource type when the backend knows it; none otherwise.
};

/// How a resource changed ([[asset::ChangeRecord]]).
enum class ChangeKind : u8
{
    Added,     ///< The resource appeared.
    Replaced,  ///< Its bytes changed.
    Removed,   ///< It is gone.
};

/// One change that [[asset::ResourceBackend::Watch]] reports.
///
/// - Thread-safety: immutable value.
struct ChangeRecord
{
    ResourceId id;                        ///< The resource that changed.
    ChangeKind kind = ChangeKind::Added;  ///< How.
};

/// Called once when an asynchronous read ends; `status` is the result.
using ReadDone = void (*)(void* user, const Status& status);

/// An open resource: ranged reads of its bytes.
///
/// A reader keeps the bytes it reads valid for its lifetime even when the backend replaces the resource meanwhile.
/// - Thread-safety: thread-compatible; `ReadAt` calls on one reader may run concurrently when the backend says so, and
///   at most one `ReadAtAsync` is in flight at a time. A reader must not be destroyed while one is in flight.
/// - Ownership: owned by the caller (a [[UniquePtr]]); it must not outlive the backend that opened it.
class ResourceReader
{
public:
    ResourceReader() = default;
    ResourceReader(const ResourceReader&) = delete;
    ResourceReader& operator=(const ResourceReader&) = delete;
    virtual ~ResourceReader() = default;

    /// The size of the resource in bytes.
    virtual u64 Size() const = 0;

    /// Reads exactly `length` bytes starting at `offset` into `buffer`.
    ///
    /// - Fails: `Invalid` when the range is not inside the resource; `Io` when the medium fails.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    virtual Status ReadAt(u64 offset, void* buffer, usize length) = 0;

    /// Reads as [[asset::ResourceReader::ReadAt]] and calls `done` once with the result, possibly before returning.
    ///
    /// The default reads synchronously. `buffer` must stay valid until `done` is called.
    ///
    /// - Ownership: `buffer` must stay valid until `done` is called; nothing else is retained.
    virtual void ReadAtAsync(u64 offset, void* buffer, usize length, ReadDone done, void* user)
    {
        done(user, ReadAt(offset, buffer, length));
    }
};

class ResourceBackend;

/// The default of [[asset::ResourceBackend::List]]: reads the backend's table and lists the ids under `prefix`.
///
/// - Fails: the status of `ReadTable`.
/// - Thread-safety: thread-safe when `backend` is.
/// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
Status ListFromTable(ResourceBackend& backend, StringView prefix, usize max, Vector<ResourceId>* out);

/// The asynchronous read of a backend's table: started by [[asset::ResourceBackend::ReadTableAsync]], finished by
/// `Finish` (the whole table) or consumed page by page with `TakePage`.
///
/// The table is complete or an error, never silently partial: a cancelled or failed read yields no table from `Finish`
/// (pages already taken stay with the caller, and the status says why the read did not finish).
/// - Thread-safety: thread-safe; `Publish` and `Complete` belong to the producer, the rest to the consumer.
/// - Ownership: owned by the caller; destroying a ticket cancels the read and waits for it to end, and the backend it
///   was started on must outlive it.
class TableTicket
{
public:
    /// Makes a ticket for a read that a producer will fill (see [[asset::TableTicket::RunBlocking]] for the usual way).
    TableTicket();
    TableTicket(const TableTicket&) = delete;
    TableTicket& operator=(const TableTicket&) = delete;
    /// Cancels the read and waits until it ended and its completion function returned.
    ~TableTicket();

    /// Starts `backend.ReadTable` on the I/O executor and returns the ticket that follows it (the form of every backend
    /// that does not read natively). When the executor refuses the work the read runs before this returns.
    static UniquePtr<TableTicket> RunBlocking(ResourceBackend& backend);

    /// True once the read ended, with success, failure or cancellation. Never blocks.
    ///
    /// - Fails: never.
    bool IsDone() const;

    /// Blocks until the read ended.
    void Wait();

    /// Asks the producer to stop; the read then ends with `Cancelled` and leaves no table.
    void Cancel();

    /// Waits for the end of the read and moves the whole table into `out` (which must be empty).
    ///
    /// - Fails: `Cancelled` after `Cancel`; otherwise the producer's status.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    Status Finish(ResourceTable* out);

    /// Adds to `out` the entries that arrived since the last call; sets `*last` when the read ended successfully and no
    /// entry is left. Every page holds complete entries. Never blocks.
    ///
    /// - Returns: the number of entries added.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    usize TakePage(ResourceTable* out, bool* last);

    /// True when `Cancel` was called (the producer polls it between pages).
    ///
    /// - Fails: never.
    bool IsCancelled() const { return cancelled_.Load(MemoryOrder::Acquire); }

    /// Registers a function called once when the read ends, on the producer's thread. The function may call `Finish`,
    /// `TakePage` and `IsDone`, but it must not destroy the ticket (the destructor waits for the function). At most one
    /// function; a second call replaces the first.
    ///
    /// - Returns: false, without registering, when the read already ended (the caller then acts as if called).
    /// - Ownership: the ticket stores `function` and `user` until the read ends; `user` must stay valid until then.
    bool SetOnComplete(void (*function)(void* user), void* user);

    /// Adds a page of complete entries (called by the producer of the read).
    ///
    /// - Fails: `Cancelled` when the consumer cancelled (the producer then calls `Complete`); `DuplicateId` when an
    /// entry
    ///   clashes with an earlier one.
    Status Publish(const ResourceTable& page);

    /// Ends the read (called by the producer of the read, exactly once).
    void Complete(Status status);

private:
    struct Job
    {
        io::IoWork work;  // first member: the executor sees the job as plain work
        TableTicket* ticket = nullptr;
        ResourceBackend* backend = nullptr;
    };

    static void RunJob(io::IoWork* work);
    void CompleteLocked(Status status);

    mutable Mutex mutex_;
    ConditionVariable ended_;
    bool done_ = false;
    bool callback_running_ = false;
    void (*on_complete_)(void* user) = nullptr;
    void* on_complete_user_ = nullptr;
    Status status_;
    UniquePtr<ResourceTable> table_;
    usize taken_ = 0;
    Atomic<bool> cancelled_{false};
    Job job_;
};

/// A source of resources. See the day-315 plan and `docs/guide/assets.md`.
///
/// Every virtual member of this class and of its helpers is defined inline, so a test double in an RTTI-enabled
/// translation unit links against the RTTI-free library.
/// - Thread-safety: thread-safe; every member may be called from any thread.
/// - Ownership: the caller owns the backend; readers and tickets it hands out must not outlive it.
class ResourceBackend
{
public:
    ResourceBackend() = default;
    ResourceBackend(const ResourceBackend&) = delete;
    ResourceBackend& operator=(const ResourceBackend&) = delete;
    virtual ~ResourceBackend() = default;

    /// A short static name for messages ("filesystem", "memory").
    ///
    /// - Ownership: returns a static string; the caller does not free it.
    /// - Fails: never.
    virtual const char* Name() const = 0;

    /// A mask of [[asset::BackendCapability]] bits.
    virtual u32 Capabilities() const = 0;

    /// Describes `id` without reading its bytes.
    ///
    /// - Fails: `NotFound` when the backend does not have `id`.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    virtual Status Stat(ResourceId id, ResourceInfo* out) = 0;

    /// Opens `id` for ranged reads.
    ///
    /// - Fails: `NotFound` when the backend does not have `id`; `Corrupt` when what it has is not the blob it promised
    ///   (the id inside disagrees).
    /// - Ownership: `*out` receives the reader on success; the caller owns it.
    virtual Status Open(ResourceId id, UniquePtr<ResourceReader>* out) = 0;

    /// Reads the backend's id <-> path table, blocking until it is complete.
    ///
    /// - Fails: the backend's status; the result is complete or an error, never silently partial.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    virtual Status ReadTable(ResourceTable* out) = 0;

    /// Starts reading the table without blocking. The default runs [[asset::ResourceBackend::ReadTable]] on the I/O
    /// executor; a backend that reads natively overrides it and sets `kBackendAsyncTable`.
    virtual UniquePtr<TableTicket> ReadTableAsync() { return TableTicket::RunBlocking(*this); }

    /// Lists at most `max` ids whose path starts with `prefix`, in path order, as a bounded view over the table.
    ///
    /// - Fails: the status of `ReadTable`.
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    virtual Status List(StringView prefix, usize max, Vector<ResourceId>* out)
    {
        return ListFromTable(*this, prefix, max, out);
    }

    /// Reports the changes since the last call (the first call reports nothing before the backend's first read).
    /// Backends without `kBackendWatch` report none.
    ///
    /// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
    virtual Status Watch(Vector<ChangeRecord>* out)
    {
        out->Clear();
        return Status::Success();
    }
};

}  // namespace engine_placeholder::asset
