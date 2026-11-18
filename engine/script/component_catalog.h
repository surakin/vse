// The component catalog (Phase 27 Day 456): which components a script can name. It wraps the host's
// `storage::ComponentRegistry` (every stored component of the engine, by name) and adds the components scripts define
// themselves (`component.define`), which it registers in the same registry so that Phase 19's storage saves them like
// any other (docs/guide/scripting.md, "Components and queries").
#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/value.h"
#include "storage/component_registry.h"
#include "string/string.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::script
{

/// The most fields a script-defined component can have.
inline constexpr usize kMaxScriptComponentFields = 32;

/// The largest size in bytes of a script-defined component (its fields, laid out in name order and padded to 16, 32,
/// 64, 128 or 256 bytes).
inline constexpr usize kMaxScriptComponentBytes = 256;

/// The lowest stored id of an engine component that scripts may change with `component.set/add/remove`; the storage
/// module's own components (transform, parent, identity, spatial: ids below it) are read-only to scripts, which have
/// bindings of their own for them (`transform.*`, `entity.*`).
inline constexpr u32 kFirstScriptWritableComponentId = 20;

/// The names scripts can use.
///
/// - Thread-safety: thread-safe. Lookups take a lock until [[script::ComponentCatalog::Freeze]], and none after it;
/// `Define` always takes it. Registering other components in the host's registry while scripts run is not allowed.
/// - Ownership: borrows the registry; owns the script definitions and the names it registers (the registry points at
/// them: see the destructor).
class ComponentCatalog
{
public:
    /// Creates a catalog over `registry`.
    ///
    /// - Ownership: borrows the registry, which must outlive the catalog; no one else registers in it while scripts
    /// are loading.
    explicit ComponentCatalog(storage::ComponentRegistry* registry);
    /// Destroys the definitions. The registry keeps pointers to the names the catalog owns, so the registry must not be
    /// used after the catalog is gone (destroy it first, or at the same time, as members declared registry-first do).
    ~ComponentCatalog();
    ComponentCatalog(const ComponentCatalog&) = delete;
    ComponentCatalog& operator=(const ComponentCatalog&) = delete;

    /// Finds a component by name: an engine component or a script-defined one.
    ///
    /// - Ownership: the pointer is owned by the registry and valid for its life.
    /// - Fails: returns null when there is none.
    const storage::RegisteredComponent* FindByName(StringView name) const;
    /// Finds a component by its stored id.
    ///
    /// - Ownership: as `FindByName`.
    /// - Fails: returns null when there is none.
    const storage::RegisteredComponent* FindById(u32 id) const;
    /// Finds the component of an ECS type.
    ///
    /// - Ownership: as `FindByName`.
    /// - Fails: returns null when there is none.
    const storage::RegisteredComponent* FindByEcsId(ecs::ComponentId id) const;

    /// Defines a component from a script: `fields` is a map from field name to type name (`f32`, `f64`, `i32`, `i64`,
    /// `u32`, `bool`, `vec2`, `vec3`, `vec4`, `quat`). The fields are laid out in name order, so every state that
    /// defines the same component gets the same layout. The component's stored id is derived from its name. Defining
    /// the same component again (the same fields) succeeds and changes nothing, so every partition's state can run the
    /// same script; a different definition under the same name, a name an engine component uses, or any new
    /// definition after `Freeze` is refused.
    ///
    /// - Param name: letters, digits and `_`, not starting with a digit, at most 48 bytes.
    /// - Param fields: a map of at most [[script::kMaxScriptComponentFields]] entries.
    /// - Param error: receives the reason when the call fails; may be null.
    /// - Returns: whether the component is defined (now or before).
    /// - Ownership: borrowed for the call.
    /// - Fails: returns false, as described.
    bool Define(StringView name, const Value& fields, String<HeapAllocator>* error);

    /// Ends the definition phase (the first tick): `Define` refuses anything new after it.
    ///
    /// - Fails: never.
    void Freeze()
    {
        LockGuard<Mutex> guard(lock_);  // a `Define` in progress ends before lookups stop taking the lock
        frozen_.Store(true, MemoryOrder::Release);
    }
    /// Whether `Freeze` was called.
    ///
    /// - Fails: never.
    bool IsFrozen() const { return frozen_.Load(MemoryOrder::Acquire); }
    /// The number of components scripts defined.
    ///
    /// - Fails: never.
    usize DefinedCount() const;

private:
    struct Definition;
    storage::ComponentRegistry* registry_;
    mutable Mutex lock_;
    Atomic<bool> frozen_{false};
    Vector<UniquePtr<Definition>> definitions_;
};

}  // namespace engine_placeholder::script
