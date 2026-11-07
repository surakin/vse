#pragma once

//! Layered sources: several stores that read as one, lowest to highest, with a way to ask where a value came from.
//!
//! The order, lowest first: the registry's defaults, then the layers by role: [[settings::LayerRole::System]] (shipped defaults),
//! `User` (the user's file, the one that is written), `Project`, `Custom` (a file named by `--settings`), `Environment`, `CommandLine`.
//! A value set in a higher layer hides the lower ones; a value missing or unusable in a layer falls through to the next lower one.

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "settings/config_store.h"
#include "settings/registry.h"
#include "string/string.h"

namespace engine_placeholder::settings {

/// Where a layer sits in the precedence order (lowest first). Layers of one role keep the order they were added in, later ones higher.
enum class LayerRole : u8 {
    System = 0,  ///< Defaults shipped with the program or the machine.
    User,        ///< The user's own file; the layer that is written by default.
    Project,     ///< A file next to the content.
    Custom,      ///< A file named on the command line (`--settings FILE`).
    Environment, ///< Environment variables.
    CommandLine, ///< `--set` and the flag aliases.
};

/// Returns a role's name (`system`, `user`, `project`, `custom`, `environment`, `command-line`).
///
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* LayerRoleName(LayerRole role);

/// How a layer fared when it was loaded.
enum class LayerState : u8 {
    NotLoaded = 0, ///< `Load` has not run.
    Loaded,        ///< Read.
    Missing,       ///< The medium does not exist (a file not written yet); treated as empty.
    Damaged,       ///< Some of it could not be understood; what could be read is used.
    Failed,        ///< It could not be read at all; skipped.
};

/// A problem found while loading or resolving: the layer, the key if there is one, and a message.
///
/// Thread-safety: thread-compatible plain data.
struct LayerProblem {
    /// The layer's name, NUL-terminated and cut to fit.
    char layer[40] = {};
    /// The key, or empty.
    char key[96] = {};
    /// What is wrong, NUL-terminated and cut to fit.
    char message[200] = {};
};

/// The answer of [[settings::LayerStack::Resolve]]: a value and where it came from.
///
/// Thread-safety: thread-compatible plain data.
struct Resolution {
    /// The value.
    Value value;
    /// The index of the layer that supplied it, or -1 for the registry's default.
    i32 layer = -1;
    /// `Clamped` if the stored value was moved into its range, else `None`.
    ValueError note = ValueError::None;
    /// The layer's name (`defaults` when `layer` is -1).
    char layer_name[40] = {};
    /// What the layer says about the entry (`line 12`, `$ENGINE_RHI_GPU`, `--gpu`), or empty.
    char origin[96] = {};
};

/// An ordered list of named layers, each a store with a role.
///
/// Thread-safety: thread-compatible.
/// Ownership: owns its stores.
class LayerStack {
public:
    /// Creates an empty stack.
    LayerStack();
    /// Destroys the stack and its stores.
    ~LayerStack();
    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    /// Adds a layer above the layers of lower or equal role.
    ///
    /// Param name: shown in provenance and reports.
    /// Param role: where it sits in the order.
    /// Param store: the store; the stack owns it.
    /// Returns: the layer's index (indices of higher layers shift when a layer is inserted below them).
    usize AddLayer(StringView name, LayerRole role, ConfigStorePtr store);
    /// Returns the number of layers.
    usize Size() const { return layers_.Size(); }
    /// Returns a layer's store, name, role and state.
    ConfigStore& Store(usize layer) { return *layers_[layer].store; }
    /// Returns a layer's name.
    StringView Name(usize layer) const { return layers_[layer].name.View(); }
    /// Returns a layer's role.
    LayerRole Role(usize layer) const { return layers_[layer].role; }
    /// Returns how a layer fared at the last `LoadAll`.
    LayerState State(usize layer) const { return layers_[layer].state; }
    /// Returns the index of the first layer with `role`, or `Size()`.
    usize Find(LayerRole role) const;

    /// Loads every layer. A layer that fails is reported and skipped; the others still load.
    ///
    /// Param problems: receives one entry per layer that was damaged or failed; may be null.
    void LoadAll(Vector<LayerProblem>* problems);

    /// Resolves one setting through the layers, highest first. A value that does not convert (a bad number in the environment) is reported in
    /// `problems` with the layer, the key and where in the layer it came from, and the next lower layer is tried.
    ///
    /// Param registry: where the declaration is.
    /// Param id: the setting.
    /// Param out: receives the value and its provenance.
    /// Param problems: receives the problems found; may be null.
    void Resolve(const Registry& registry, SettingId id, Resolution* out, Vector<LayerProblem>* problems);

    /// Resolves the value the layers *below* `layer` give (to decide whether a write is needed).
    void ResolveBelow(const Registry& registry, SettingId id, usize layer, Resolution* out);

    /// Returns the index of the layer that is written (the first `User` layer unless [[settings::LayerStack::SetWriteLayer]] chose another), or `Size()` if there is none.
    usize WriteLayer() const;
    /// Chooses the written layer.
    ///
    /// Aborts: `layer` is not less than `Size()`.
    void SetWriteLayer(usize layer);

    /// Sets a value in the write layer. With `minimal` a value equal to what the lower layers already give is not written, and an earlier
    /// entry for the key is removed instead, so the file does not fill with defaults.
    ///
    /// Fails: `NotFound` (no write layer), `ReadOnly`, `TooLong`, and the store's own errors.
    Status Set(const Registry& registry, SettingId id, const Value& value, bool minimal);
    /// Removes the key from the write layer so a lower layer shows again.
    ///
    /// Returns: `Ok` also when the key was not there.
    Status Unset(const Registry& registry, SettingId id);

private:
    struct Layer {
        String<HeapAllocator> name = String<HeapAllocator>(HeapAllocator::Default());
        LayerRole role = LayerRole::User;
        ConfigStorePtr store;
        LayerState state = LayerState::NotLoaded;
    };
    bool ResolveFrom(const Registry& registry, SettingId id, usize below, Resolution* out, Vector<LayerProblem>* problems);
    Vector<Layer> layers_;
    usize write_layer_ = static_cast<usize>(-1);
};

/// Finds the registered key closest to `text` (by edit distance), for "did you mean".
///
/// Param registry: the keys to search (deprecated keys are not offered).
/// Param text: what was typed.
/// Param out: receives the suggestion.
/// Returns: whether a key is close enough to be worth suggesting (distance at most a third of the length, at least 3).
bool SuggestKey(const Registry& registry, StringView text, Key* out);

} // namespace engine_placeholder::settings
