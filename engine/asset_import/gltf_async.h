#pragma once

//! The asynchronous glTF import.
//!
//! Phase 22 Day 338. [[asset::ImportGltfAsync]] runs [[asset::ImportGltf]] as a job on the job system and returns a
//! ticket to poll, wait on, cancel and take the result from; the destructor cancels and waits, so a ticket never
//! outlives its job. Several imports may run at once: they share nothing.

#include "asset_import/gltf_import.h"
#include "containers/unique_ptr.h"
#include "jobs/counter.h"
#include "jobs/scheduler.h"

namespace engine_placeholder::asset
{

struct ImportTicketState;

/// A running import.
///
/// - Thread-safety: thread-safe: any thread may poll, cancel and wait; `Finish` once.
/// - Ownership: owns the file bytes, the options and the result; borrows the source, the id source and the manifest of
/// the options, which must stay valid
///   until the ticket is destroyed.
class ImportTicket
{
public:
    /// Makes an idle ticket; use [[asset::ImportGltfAsync]], which starts the import.
    explicit ImportTicket(jobs::Scheduler& scheduler);
    ImportTicket(const ImportTicket&) = delete;
    ImportTicket& operator=(const ImportTicket&) = delete;
    /// Cancels the import and waits until its job ended.
    ~ImportTicket();

    /// True once the job ended (with success, failure or cancellation). Never blocks.
    ///
    /// - Fails: never.
    bool IsDone() const;

    /// The fraction of the import's steps finished, 0 to 1. Never blocks.
    ///
    /// - Fails: never.
    f32 Progress() const;

    /// Asks the job to stop at its next step; the import then ends with `Cancelled` and makes no result.
    ///
    /// - Fails: never.
    void Cancel();

    /// Waits for the job (helping it when the scheduler has one participant) and takes the result.
    ///
    /// - Param result: replaced by the result of a successful import.
    /// - Param error: receives the sentence of a refusal.
    /// - Fails: what [[asset::ImportGltf]] reports, or `Cancelled`.
    /// - Ownership: moves the result into `*result`; a second call fails `NotFound`.
    Status Finish(ImportResult* result, GltfError* error);

private:
    friend UniquePtr<ImportTicket> ImportGltfAsync(jobs::Scheduler& scheduler, Vector<byte> file, GltfSource& source,
                                                   const ImportOptions& options);
    UniquePtr<ImportTicketState> state_;
};

/// Starts an import on the job system.
///
/// - Param scheduler: runs the job.
/// - Param file: the `.gltf` or `.glb` bytes (moved into the ticket).
/// - Param source: reads the external files; used from the job's thread, so it must be thread-safe or used by this
/// import alone; must outlive the ticket.
/// - Param options: limits, ids, manifest; `cancel` and `steps_done` are the ticket's own and replaced. The pointers
/// inside must outlive the ticket.
/// - Fails: reported through the ticket.
/// - Ownership: the ticket owns what it is given by value.
UniquePtr<ImportTicket> ImportGltfAsync(jobs::Scheduler& scheduler, Vector<byte> file, GltfSource& source,
                                        const ImportOptions& options);

}  // namespace engine_placeholder::asset
