// The settings bindings (Phase 27 Day 454): `settings.get/set/exists/describe/on_change`. Reads come from the snapshot
// taken at the tick start; writes are commands applied at the sync point; a script may set only the prefixes its policy
// allows (docs/guide/scripting.md, "Settings").
#pragma once

#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/bindings.h"
#include "script/merge.h"
#include "settings/registry.h"
#include "settings/settings.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The command kind `settings.set` records (see [[script::RegisterSettingsCommandKind]]).
inline constexpr u16 kCommandSettingsSet = 200;

/// The most subscriptions one entity may hold.
inline constexpr u32 kMaxSettingsSubscriptionsPerEntity = 32;
/// The most subscriptions one partition may hold in all.
inline constexpr u32 kMaxSettingsSubscriptions = 4096;

/// Which setting keys a script may change. A prefix names a key and everything below it: `game` or `game.*` covers
/// `game` and `game.volume`, not `gamepad.x`.
///
/// - Thread-safety: thread-compatible; read-only while the script phase runs, so partitions share one.
/// - Ownership: owns its strings.
class SettingsPolicy
{
public:
    /// A policy that lets every script set `game.*` and nothing else.
    SettingsPolicy();

    /// Replaces the prefixes every script may set unless it has its own list.
    ///
    /// - Param prefixes: the allowed prefixes; empty allows nothing.
    /// - Ownership: copied.
    /// - Fails: never.
    void SetDefaultWritable(Span<const StringView> prefixes);

    /// Gives one script (named by its chunk name) its own list, replacing the default for it.
    ///
    /// - Param script: the script's name.
    /// - Param prefixes: the allowed prefixes.
    /// - Ownership: copied.
    /// - Fails: never.
    void SetScriptWritable(StringView script, Span<const StringView> prefixes);

    /// Whether `script` may set `key`.
    ///
    /// - Thread-safety: thread-safe once filled.
    /// - Fails: never.
    bool CanWrite(StringView script, StringView key) const;

private:
    struct ScriptList
    {
        String<HeapAllocator> script{HeapAllocator::Default()};
        Vector<String<HeapAllocator>> prefixes;
    };
    static void Fill(Vector<String<HeapAllocator>>* out, Span<const StringView> prefixes);
    static bool Covers(const Vector<String<HeapAllocator>>& list, StringView key);

    Vector<String<HeapAllocator>> default_;
    Vector<ScriptList> scripts_;
};

/// One `settings.on_change` subscription.
///
/// - Thread-safety: thread-compatible plain data.
struct SettingsSubscription
{
    /// The stable id of the entity whose script subscribed.
    u64 entity_key = 0;
    /// The key or prefix watched.
    String<HeapAllocator> prefix{HeapAllocator::Default()};
    /// The global function (a dotted path) to call.
    String<HeapAllocator> function{HeapAllocator::Default()};
};

/// What the settings bindings of one partition see: the snapshot taken at the tick start, the policy, and the
/// partition's own subscriptions.
///
/// - Thread-safety: thread-compatible; one job uses it at a time, like the partition.
/// - Ownership: borrows the registry and the policy, which must outlive it; shares the snapshot.
struct SettingsAccess
{
    /// The declarations (never null while published).
    const settings::Registry* registry = nullptr;
    /// The values as of the tick start.
    SharedPtr<const settings::SettingsSnapshot> snapshot;
    /// Which keys scripts may set (may be null: nothing is writable).
    const SettingsPolicy* policy = nullptr;
    /// The partition's subscriptions, in the order they were made.
    Vector<SettingsSubscription> subscriptions;
};

/// One delivery the sync point owes a subscriber.
///
/// - Thread-safety: thread-compatible plain data.
struct SettingNotification
{
    /// The partition that holds the subscription.
    u32 partition = 0;
    /// The subscribing entity's stable id.
    u64 entity_key = 0;
    /// The function to call.
    String<HeapAllocator> function{HeapAllocator::Default()};
    /// The key that changed.
    String<HeapAllocator> key{HeapAllocator::Default()};
};

/// Registers the command that applies `settings.set` at the merge. The merge's `user` pointer must be a
/// [[script::SettingsMergeServices]].
///
/// - Param kinds: the table to add to; not null.
/// - Returns: false when the id is taken.
/// - Ownership: borrowed for the call.
/// - Fails: returns false, as described.
bool RegisterSettingsCommandKind(CommandKindTable* kinds);

/// What the settings command needs at the merge.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the settings, which must outlive the merge.
struct SettingsMergeServices
{
    /// Where the values are set.
    settings::Settings* settings = nullptr;
};

/// Lists, for every subscription of every partition, the keys whose value differs between two snapshots, in the order
/// partition, subscription, key: so the sync point delivers the same notifications for any thread count.
///
/// - Param registry: the declarations; keys come in the registry's key order.
/// - Param before: the snapshot at the tick start.
/// - Param after: the snapshot after the merge.
/// - Param partitions: every partition's access, in partition order (null entries are skipped).
/// - Returns: the notifications.
/// - Ownership: borrowed for the call; the result owns its strings.
/// - Thread-safety: thread-compatible.
/// - Fails: never.
Vector<SettingNotification> CollectSettingChanges(const settings::Registry& registry,
                                                  const settings::SettingsSnapshot& before,
                                                  const settings::SettingsSnapshot& after,
                                                  Span<SettingsAccess* const> partitions);

/// Removes every subscription of an entity (call it when the entity or its script is destroyed).
///
/// - Param access: the partition's access.
/// - Param entity_key: the entity's stable id.
/// - Returns: how many were removed.
/// - Ownership: borrowed for the call.
/// - Fails: never.
usize RemoveSettingsSubscriptions(SettingsAccess* access, u64 entity_key);

/// Adds `settings.get`, `settings.set`, `settings.exists`, `settings.describe` and `settings.on_change`. They read the
/// running job's [[script::PartitionContext::settings]]; `get`, `exists` and `describe` are `pure` (they read the
/// tick-start snapshot), `set` and `on_change` are `world-write` (a command and a partition-local subscription).
///
/// - Param registry: the registry to add to; not null.
/// - Returns: true when all were added.
/// - Ownership: borrowed for the call.
/// - Fails: returns false when a name is taken.
bool RegisterSettingsBindings(BindingRegistry* registry);

}  // namespace engine_placeholder::script
