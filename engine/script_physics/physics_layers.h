// The physics layer names (Phase 27 Day 466): scripts name collision layers, never raw bits. The table is a setting
// (`script.physics_layers`: the names in order, separated by commas; a name's position is its layer number, 0 to 31),
// so a game declares `default,world,player,enemy,projectile` once and its scripts write `"player"`.
#pragma once

#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "script/value.h"
#include "settings/registry.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::script_physics
{

/// The most layers (the physics module's layer numbers are 0 to 31).
inline constexpr usize kMaxPhysicsLayers = 32;

/// The layer names scripts can use.
///
/// - Thread-safety: thread-compatible; fill it, then share it read-only (the bindings only read it).
/// - Ownership: owns the names.
class PhysicsLayers
{
public:
    /// Reads a table from text: names separated by commas, each letters, digits and `_` (not starting with a digit),
    /// at most 32 of them, none twice; blanks around a name are ignored. An empty text is the table `default`.
    ///
    /// - Param text: the names.
    /// - Param error: receives the reason when the text is refused; may be null.
    /// - Returns: whether the table was replaced; false leaves it as it was.
    /// - Ownership: `text` is read during the call only.
    /// - Fails: returns false, as described.
    bool Parse(StringView text, String<HeapAllocator>* error);

    /// The number of layers.
    ///
    /// - Fails: never.
    usize Count() const { return names_.Size(); }
    /// The name of layer `index`.
    ///
    /// - Param index: below `Count()`.
    /// - Aborts: `index` is not below `Count()`.
    StringView NameOf(usize index) const { return names_[index].View(); }

    /// Finds a layer by name.
    ///
    /// - Param name: the name.
    /// - Param index: receives the layer number.
    /// - Returns: false when there is no such layer.
    /// - Ownership: borrowed for the call.
    /// - Fails: returns false, as described.
    bool Find(StringView name, u32* index) const;

    /// Turns a script's layer selection into the bit mask the physics queries take: nil is every layer; text is one
    /// layer; an array of text is those layers.
    ///
    /// - Param selection: the script's value.
    /// - Param mask: receives the mask.
    /// - Param error: receives the reason on failure (an unknown name, a wrong kind); may be null.
    /// - Returns: whether the selection was understood.
    /// - Ownership: borrowed for the call.
    /// - Fails: returns false, as described.
    bool MaskOf(const script::Value& selection, u32* mask, String<HeapAllocator>* error) const;

private:
    Vector<String<HeapAllocator>> names_;
};

/// Declares `script.physics_layers` (default `default`; restart required).
///
/// - Param registry: the registry to declare it in; not null.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterPhysicsScriptSettings(settings::Registry* registry);

/// Reads the layer table from the settings; a text that does not parse keeps the table `default`.
///
/// - Param registry: a registry in which [[script_physics::RegisterPhysicsScriptSettings]] was called.
/// - Param layers: receives the table; not null.
/// - Returns: whether the setting parsed (or was absent).
/// - Ownership: borrowed for the call.
/// - Fails: returns false and leaves `default`, as described.
bool PhysicsLayersFromSettings(const settings::Registry& registry, PhysicsLayers* layers);

}  // namespace engine_placeholder::script_physics
