#pragma once

//! Typed handles for reading a setting: `Setting<bool>`, `Setting<i64>`, `Setting<f32>`, `Setting<f64>` and
//! `Setting<E>` for an enum type `E`, and [[settings::TextSetting]] for text.
//!
//! A handle is one pointer. `Get()` is one relaxed atomic load and a decode, so it is safe and cheap in a hot path from
//! any thread; it sees a published value on the next call after `Registry::Publish` returns. A handle made for the
//! wrong type aborts at creation.

#include "lang/traits.h"

#include "log/assert.h"
#include "settings/registry.h"
#include "sync/atomic.h"
#include "cstr/mem_ops.h"

namespace vse::settings
{

namespace detail
{

template <typename T> constexpr Type TypeOf()
{
    if constexpr (IsSame<T, bool>)
    {
        return Type::Bool;
    }
    else if constexpr (IsSame<T, i64>)
    {
        return Type::Int;
    }
    else if constexpr (IsSame<T, f32> || IsSame<T, f64>)
    {
        return Type::Float;
    }
    else
    {
        static_assert(IsEnum<T>, "Setting<T>: T must be bool, i64, f32, f64 or an enum type");
        return Type::Enum;
    }
}

template <typename T> T Decode(u64 bits)
{
    if constexpr (IsSame<T, bool>)
    {
        return bits != 0;
    }
    else if constexpr (IsSame<T, i64>)
    {
        i64 v;
        MemCopy(&v, &bits, sizeof(v));
        return v;
    }
    else if constexpr (IsSame<T, f32> || IsSame<T, f64>)
    {
        f64 v;
        MemCopy(&v, &bits, sizeof(v));
        return static_cast<T>(v);
    }
    else
    {
        return static_cast<T>(bits);
    }
}

/// Encodes a value of a declared type into the cell's 64 bits.
u64 Encode(const Value& value);

}  // namespace detail

/// A typed, cheap-to-read handle on one registered setting.
///
/// - Thread-safety: thread-safe; any thread may copy it and call `Get`.
/// - Ownership: borrows the registry, which must outlive the handle.
template <typename T> class Setting
{
public:
    /// Creates an empty handle; `Get` on it aborts.
    Setting() = default;

    /// Makes a handle for a registered setting.
    ///
    /// - Param registry: the registry that holds it.
    /// - Param id: the setting.
    /// - Aborts: `id` is not registered, or its declared type differs from `T`.
    Setting(const Registry& registry, SettingId id) : registry_(&registry), id_(id)
    {
        const SettingDesc* d = registry.Desc(id);
        ENGINE_ASSERT(d != nullptr, "settings::Setting: the id is not registered");
        ENGINE_ASSERT(d->type == detail::TypeOf<T>(),
                      "settings::Setting: the setting has another type than the handle");
        cell_ = registry.CellOf(id);
    }

    /// Makes a handle for a setting found by key.
    ///
    /// - Aborts: no such key, or the declared type differs from `T`.
    static Setting Find(const Registry& registry, StringView key) { return Setting(registry, registry.Find(key)); }

    /// Returns whether the handle names a setting (a default-constructed one does not).
    bool IsBound() const { return cell_ != nullptr; }

    /// Returns the current value: one relaxed atomic load.
    ///
    /// - Aborts: the handle is empty.
    T Get() const
    {
        ENGINE_ASSERT(cell_ != nullptr, "settings::Setting::Get: empty handle");
        return detail::Decode<T>(cell_->bits.Load(MemoryOrder::Relaxed));
    }
    /// Returns the default.
    ///
    /// - Aborts: the handle is empty.
    T Default() const { return detail::Decode<T>(detail::Encode(Desc().default_value)); }
    /// Returns the declaration.
    ///
    /// - Aborts: the handle is empty.
    const SettingDesc& Desc() const
    {
        ENGINE_ASSERT(registry_ != nullptr, "settings::Setting::Desc: empty handle");
        return *registry_->Desc(id_);
    }
    /// Returns the setting's id.
    SettingId Id() const { return id_; }

private:
    const Registry* registry_ = nullptr;
    const Registry::Cell* cell_ = nullptr;
    SettingId id_;
};

/// A handle on a text setting. Reading copies under a lock, so it is for configuration code, not for a hot path.
///
/// - Thread-safety: thread-safe.
/// - Ownership: borrows the registry.
class TextSetting
{
public:
    /// Makes a handle.
    ///
    /// - Aborts: `id` is not registered or is not a text setting.
    TextSetting(const Registry& registry, SettingId id) : registry_(&registry), id_(id)
    {
        const SettingDesc* d = registry.Desc(id);
        ENGINE_ASSERT(d != nullptr && d->type == Type::Text, "settings::TextSetting: not a registered text setting");
    }
    /// Copies the current text into `out` (see [[settings::Registry::CurrentText]]).
    usize Get(char* out, usize capacity) const { return registry_->CurrentText(id_, out, capacity); }
    /// Returns the setting's id.
    SettingId Id() const { return id_; }

private:
    const Registry* registry_;
    SettingId id_;
};

}  // namespace vse::settings
