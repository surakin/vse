#pragma once

#include "rhi/device_host.h"
#include "types.h"

//! The registry of everything in the renderer that owns device objects and can rebuild them after the device is replaced.

namespace engine_placeholder::renderer {

/// The tiers owners register at; lower tiers are created first and released last.
///
/// An owner may use a value between these. The order is fixed so that an owner never sees a dependency missing: a mesh needs
/// nothing but the device, a material needs its textures and samplers, a pass needs its materials.
namespace tiers {
/// Samplers and textures.
inline constexpr u32 kTextures = 0;
/// Meshes (vertex and index buffers).
inline constexpr u32 kMeshes = 1;
/// Bind group layouts and shaders.
inline constexpr u32 kLayouts = 2;
/// Materials and pipelines.
inline constexpr u32 kMaterials = 3;
/// Passes and per-frame buffers.
inline constexpr u32 kPasses = 4;
} // namespace tiers

/// An object that owns device objects and keeps what it needs to make them again.
///
/// The contract: `Release` destroys every device object and keeps the CPU-side source; `Recreate` makes them again on a
/// device and re-uploads; neither caches a handle past a frame for anyone else. Both are called on the owning thread, with the
/// device idle.
///
/// Thread-safety: owner-thread-only.
class Recreatable {
public:
    virtual ~Recreatable() = default;

    /// Destroys every device object this owner made; keeps the CPU-side source. Safe to call when already released.
    virtual void Release() = 0;

    /// Makes the device objects again.
    ///
    /// Param device: the new device.
    /// Returns: true on success; on failure nothing stays allocated.
    /// Fails: returns false, logging the reason, if the source is gone or the device cannot hold the object (limits, memory).
    /// Ownership: the device is borrowed; it outlives the objects until the next `Release`.
    virtual bool Recreate(rhi::Device* device) = 0;

    /// Returns whether the owner currently holds its device objects. The registry asks when the owner registers, so an owner
    /// that registers after it made its objects is released at the next reset.
    ///
    /// Fails: never.
    virtual bool Live() const = 0;

    /// Returns a short name for logs and failure reports.
    ///
    /// Returns: a string that stays valid as long as the owner.
    /// Fails: never.
    /// Ownership: the string belongs to the owner.
    virtual const char* RecreatableName() const = 0;
};

/// Rebuilds registered owners in tier order whenever the device host replaces the device.
///
/// Register it with [[rhi::DeviceHost::AddObserver]]; `OnDeviceReleasing` releases owners from the highest tier down,
/// `OnDeviceCreated` recreates them from the lowest up. A failed `Recreate` stops the sequence, releases everything that was
/// built (a half-built set is never used), remembers the owner and returns false so the host fails the reset (the caller rolls
/// back). Calling it again starts from released.
///
/// Thread-safety: owner-thread-only.
/// Ownership: borrows the owners; an owner must unregister (see [[renderer::ScopedRegistration]]) before it is destroyed.
///   The registry must outlive every registration.
class DeviceResources final : public rhi::DeviceObserver {
public:
    /// The most owners.
    static constexpr usize kMaxOwners = 256;

    DeviceResources() = default;
    DeviceResources(const DeviceResources&) = delete;
    DeviceResources& operator=(const DeviceResources&) = delete;

    /// Registers an owner.
    ///
    /// The owner counts as live if `Live()` says it holds its objects now; one that does not is recreated at the next
    /// `OnDeviceCreated`.
    ///
    /// Param owner: borrowed; must stay alive until `Unregister`.
    /// Param tier: its order, see the `tiers` namespace.
    /// Returns: true if registered.
    /// Fails: returns false, logging the reason, if the table is full or a reset is running.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool Register(Recreatable* owner, u32 tier);

    /// Removes an owner; unknown owners are ignored. Does not release the owner's objects: the owner does that itself.
    /// During a reset the entry is only marked dead and removed when the reset step ends, so an owner may unregister itself or
    /// another from inside `Release` or `Recreate`.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    void Unregister(Recreatable* owner);

    /// Replaces a registered owner by another at the same tier and state (an owner that moved).
    /// Param from: the old address.
    /// Param to: the new address.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    void Rebind(Recreatable* from, Recreatable* to);

    /// Returns the number of registered owners.
    usize Count() const { return count_; }

    /// Returns the name of the owner whose `Recreate` failed last, or null.
    ///
    /// Ownership: the string belongs to that owner and is valid until it is destroyed.
    /// Fails: never.
    const char* FailedOwner() const { return failed_name_; }

    /// Returns the device the registry last recreated on, or null while released.
    /// Fails: never.
    /// Ownership: the returned pointer is borrowed from the owner.
    rhi::Device* CurrentDevice() const { return device_; }

    /// Releases every live owner, highest tier first (the first half of a reset; the host calls it).
    ///
    /// Param epoch: the serial of the device being released.
    void OnDeviceReleasing(u32 epoch) override;

    /// Recreates every released owner, lowest tier first.
    ///
    /// Param device: the new device.
    /// Param epoch: its serial.
    /// Returns: false if an owner failed; everything is released again and `FailedOwner` names it.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool OnDeviceCreated(rhi::Device* device, u32 epoch) override;

    /// Returns "device-resources".
    /// Fails: never.
    /// Ownership: the returned pointer is borrowed from the owner.
    const char* ObserverName() const override { return "device-resources"; }

private:
    struct Entry {
        Recreatable* owner = nullptr;
        u32 tier = 0;
        u32 sequence = 0;
        bool live = false;
    };

    void ReleaseAllLive();
    void Compact();

    Entry entries_[kMaxOwners];
    usize count_ = 0;
    u32 next_sequence_ = 0;
    rhi::Device* device_ = nullptr;
    bool resetting_ = false;
    bool compact_pending_ = false;
    const char* failed_name_ = nullptr;
};

/// Keeps an owner registered for exactly as long as this object lives.
///
/// Put one in each owner. It moves with the owner (call `Rebind` from the owner's move operations) and unregisters in its
/// destructor, so a destroyed owner never gets a callback.
///
/// Thread-safety: owner-thread-only.
/// Ownership: borrows the registry and the owner.
class ScopedRegistration {
public:
    ScopedRegistration() = default;
    /// Releases everything this object owns.
    ~ScopedRegistration() { Reset(); }
    ScopedRegistration(const ScopedRegistration&) = delete;
    ScopedRegistration& operator=(const ScopedRegistration&) = delete;

    /// Registers `owner` with `registry` (or does nothing if `registry` is null).
    ///
    /// Param registry: the registry; may be null for an owner that does not need to survive a reset.
    /// Param owner: the owner.
    /// Param tier: its tier.
    /// Returns: true if registered or `registry` is null.
    /// Fails: returns false if the registry refused.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    bool Attach(DeviceResources* registry, Recreatable* owner, u32 tier) {
        Reset();
        if (registry == nullptr) {
            return true;
        }
        if (!registry->Register(owner, tier)) {
            return false;
        }
        registry_ = registry;
        owner_ = owner;
        return true;
    }

    /// Takes over the registration of `other`, whose owner moved to `new_owner`; `other` is left unregistered.
    /// Ownership: borrowed for the call; nothing is kept unless the description above says so.
    void Adopt(ScopedRegistration* other, Recreatable* new_owner) {
        Reset();
        if (other->registry_ != nullptr) {
            other->registry_->Rebind(other->owner_, new_owner);
            registry_ = other->registry_;
            owner_ = new_owner;
            other->registry_ = nullptr;
            other->owner_ = nullptr;
        }
    }

    /// Unregisters now.
    void Reset() {
        if (registry_ != nullptr) {
            registry_->Unregister(owner_);
            registry_ = nullptr;
            owner_ = nullptr;
        }
    }

    /// Returns whether an owner is registered through this object.
    /// Fails: never.
    bool Registered() const { return registry_ != nullptr; }

private:
    DeviceResources* registry_ = nullptr;
    Recreatable* owner_ = nullptr;
};

} // namespace engine_placeholder::renderer
