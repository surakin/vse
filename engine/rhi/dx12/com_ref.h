#pragma once

//! A small owning reference to a COM object, so the DX12 backend needs no WRL `ComPtr` (design spec decision 10).

namespace vse::rhi::dx12
{

/// Owns one reference to a COM object of type `T` (any type with `AddRef` and `Release`): move-only, released on
/// destruction.
///
/// `Put()` hands out the address of the raw pointer for the `void**`/`T**` out-parameters of COM calls, releasing what
/// was held first; `Get()` borrows. Copies are deliberately impossible: a second owner takes a reference with
/// `Share()`.
///
/// - Thread-safety: thread-compatible; the referenced object follows its own COM rules.
template <typename T> class ComRef
{
public:
    /// Holds nothing.
    ComRef() = default;
    /// Takes over a reference the caller owns (does not add one).
    ///
    /// - Param raw: an owned reference, or null.
    explicit ComRef(T* raw) : raw_(raw) {}
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;
    /// Takes the other's reference; the other holds nothing afterwards.
    ComRef(ComRef&& other) noexcept : raw_(other.raw_) { other.raw_ = nullptr; }
    /// Releases what this held and takes the other's reference.
    ComRef& operator=(ComRef&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            raw_ = other.raw_;
            other.raw_ = nullptr;
        }
        return *this;
    }
    /// Releases the reference.
    ~ComRef() { Reset(); }

    /// Releases the reference and holds nothing.
    void Reset()
    {
        if (raw_ != nullptr)
        {
            T* raw = raw_;
            raw_ = nullptr;
            raw->Release();
        }
    }
    /// Returns a new owner of the same object (adds a reference).
    ///
    /// - Returns: an owner, or an empty one when this holds nothing.
    ComRef Share() const
    {
        if (raw_ != nullptr)
        {
            raw_->AddRef();
        }
        return ComRef(raw_);
    }
    /// Gives up the reference without releasing it.
    ///
    /// - Returns: the raw pointer; the caller owns its reference.
    T* Detach()
    {
        T* raw = raw_;
        raw_ = nullptr;
        return raw;
    }
    /// Releases what is held and returns the address to receive a new reference (for COM out-parameters).
    ///
    /// - Returns: the address of the raw pointer.
    T** Put()
    {
        Reset();
        return &raw_;
    }
    /// Like `Put`, typed for `QueryInterface`-style `void**` parameters.
    ///
    /// - Returns: the address of the raw pointer as `void**`.
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }
    /// Returns the raw pointer, still owned by this.
    T* Get() const { return raw_; }
    /// Accesses the object.
    T* operator->() const { return raw_; }
    /// Returns whether a reference is held.
    explicit operator bool() const { return raw_ != nullptr; }

private:
    T* raw_ = nullptr;
};

}  // namespace vse::rhi::dx12
