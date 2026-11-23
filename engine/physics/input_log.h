// Recording and replaying the program's inputs to a physics world (Phase 25 Day 406): forces, impulses, velocities,
// teleports, wake-ups and creations and destructions of bodies, each with the step number it was applied at. A run
// restored from a snapshot and fed the log from that step on reproduces the original run when every input the program
// gave went through the recorder. Only the seven kinds of `InputKind` are recorded: adding a collider alone, changing a
// body's type or settings, joints, characters, `SetStepConfig` and kinematic targets are not, and a replay of a program
// that makes them diverges (`KNOWN-ISSUES.md`, Phase 25 closing).
#pragma once

#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/snapshot_status.h"
#include "physics/world.h"
#include "types.h"

namespace vse::physics
{

/// The kinds of input the log holds. The numbers are written in logs and never renumbered.
enum class InputKind : u8
{
    ApplyForce = 0,    ///< `ApplyForce(body, vector, point)`.
    ApplyImpulse = 1,  ///< `ApplyImpulse(body, vector, point)`.
    SetVelocity = 2,   ///< `SetVelocity(body, linear, angular)`.
    SetTransform = 3,  ///< `SetTransform(body, position, orientation)`.
    Wake = 4,          ///< `Wake(body)`.
    CreateBody = 5,    ///< `CreateBody(desc)` and, with `has_collider`, `AddCollider(collider)`.
    DestroyBody = 6,   ///< `DestroyBody(body)`.
};

/// One input: what was done, to which body, at which step.
///
/// - Thread-safety: thread-compatible plain data.
struct InputEntry
{
    /// The number of steps the world had run when the input was applied (`GetStepStats().steps`).
    u64 step = 0;
    /// What it was.
    InputKind kind = InputKind::ApplyForce;
    /// The body it was applied to.
    BodyId body;
    /// The vector (force, impulse, linear velocity).
    Vec3 a = Vec3(0.0f, 0.0f, 0.0f);
    /// The angular velocity.
    Vec3 b = Vec3(0.0f, 0.0f, 0.0f);
    /// The point (force, impulse) or the position (`SetTransform`), in the world.
    Vec3d p = Vec3d(0.0, 0.0, 0.0);
    /// The orientation (`SetTransform`).
    Quaternion q = Quaternion::Identity();
    /// The body made (`CreateBody`).
    BodyDesc desc;
    /// Whether `collider` was added to the body made.
    bool has_collider = false;
    /// The collider added.
    Collider collider;
};

/// A list of inputs in the order they were applied.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its entries.
class InputLog
{
public:
    /// The entries, in order; the step numbers never decrease.
    /// - Fails: never.
    const Vector<InputEntry>& Entries() const { return entries_; }

    /// Adds an entry.
    ///
    /// - Param entry: the entry; its step must not be below the last entry's.
    /// - Returns: false when the step goes back (the entry is not added).
    /// - Fails: as the return says.
    bool Add(const InputEntry& entry);

    /// Writes the log in the canonical encoding (a magic, a version, a count and the entries).
    ///
    /// - Param out: the writer.
    /// - Ownership: borrows the writer for the call.
    /// - Fails: never.
    void Save(bytes::ByteWriter* out) const;

    /// Replaces the log by one read from bytes.
    ///
    /// - Param data: the bytes `Save` wrote.
    /// - Returns: `Ok`, or why not (the log is then empty).
    /// - Ownership: `data` is read during the call only.
    /// - Fails: as the return says; a hostile log is refused, never trusted.
    SnapshotStatus Load(ByteSpan data);

private:
    Vector<InputEntry> entries_;
};

/// Applies inputs to a world and logs them: the calls take the arguments of the world's own and return what they
/// return.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the world and the log; they must outlive the recorder.
class InputRecorder
{
public:
    /// Makes a recorder.
    /// - Param world: the world the inputs go to.
    /// - Param log: the log they are appended to.
    /// - Ownership: borrows both; they must outlive the recorder.
    /// - Fails: never.
    InputRecorder(PhysicsWorld* world, InputLog* log) : world_(world), log_(log) {}

    /// `PhysicsWorld::ApplyForce`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus ApplyForce(BodyId id, const Vec3& force, const Vec3d& point);
    /// `PhysicsWorld::ApplyImpulse`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3d& point);
    /// `PhysicsWorld::SetVelocity`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus SetVelocity(BodyId id, const Velocity& velocity);
    /// `PhysicsWorld::SetTransform`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus SetTransform(BodyId id, const Vec3d& position, const Quaternion& orientation);
    /// `PhysicsWorld::Wake`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus Wake(BodyId id);
    /// `PhysicsWorld::CreateBody` and `AddCollider` in one call, logged when both succeed.
    /// - Param desc: the body.
    /// - Param collider: its collider.
    /// - Param status: receives the status.
    /// - Returns: the body, or the null id.
    /// - Ownership: the world owns the body.
    /// - Fails: as the world's; a body whose collider is refused is destroyed again and not logged.
    BodyId CreateBody(const BodyDesc& desc, const Collider& collider, BodyStatus* status = nullptr);
    /// `PhysicsWorld::DestroyBody`, logged when it succeeds.
    /// - Fails: as the world's.
    BodyStatus DestroyBody(BodyId id);

private:
    void Log(InputEntry* entry);
    PhysicsWorld* world_;
    InputLog* log_;
};

/// Applies the log's entries that belong to the world's current step (`GetStepStats().steps`), then runs one step.
///
/// - Param world: the world.
/// - Param log: the log.
/// - Param cursor: the index of the next entry to apply; start at 0 for a world at the log's first step, or at the
///   index `FirstEntryAt` returns for a restored world. Advanced past the entries applied.
/// - Returns: the number of entries applied (an entry the world refuses is skipped and counted as applied).
/// - Ownership: nothing is kept.
/// - Fails: never.
u32 ReplayStep(PhysicsWorld* world, const InputLog& log, usize* cursor);

/// The index of the first entry at or after a step: the cursor a restored world starts replaying from.
///
/// - Param log: the log.
/// - Param step: the number of steps the restored world has run.
/// - Returns: the index.
/// - Fails: never.
usize FirstEntryAt(const InputLog& log, u64 step);

}  // namespace vse::physics
