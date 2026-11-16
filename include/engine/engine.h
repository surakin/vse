// engine.h: a thin C++ layer over the C API (engine_c.h), for hosts written in C++. Header-only, no exceptions, no
// RTTI, no standard library: it includes engine_c.h and nothing else, so a host copies engine_c.h, engine_abi_version.h
// and this file.
//
// It adds three things the C header cannot enforce and nothing else: ownership (a handle is destroyed exactly once, a
// buffer released exactly once), struct sizing (`Init<T>()` zeroes a struct and sets `struct_size`), and a status that
// cannot be dropped silently by accident (`[[nodiscard]]`). It adds no ABI: every call is the table's function, called
// through the pointer you give it, and the C header stays the contract. A function the layer does not wrap is called
// through `api->function` with the same handles (`handle.Get()`).
//
//     const engine_api* api = nullptr;
//     engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api);   // or
//     engine_loader.h engine_host::Engine engine; auto info = engine_host::Init<engine_create_info>(); if
//     (!engine_host::Engine::Create(api, info, &engine)) { ... } engine_host::World world; engine.CreateWorld(&world);
//     // destroyed before `engine`, by the declaration order
//
// Rules the layer keeps: a handle object is move-only; destroying it (the destructor, `Reset()`) calls the table's
// destroy function and ignores its status (a destructor cannot report; call `Destroy()` to see it); a handle object
// must be destroyed before the engine's own `Destroy` (the engine refuses with IN_USE while a world, surface or texture
// exists), which declaring it after the engine in the same scope gives; and the table pointer must outlive every object
// (it is static for as long as the library stays loaded).
#ifndef ENGINE_H
#define ENGINE_H

#include "engine_c.h"

namespace engine_host
{

/// A status as a value: true when it is ENGINE_OK. `[[nodiscard]]` so a dropped failure is a compile-time warning.
class [[nodiscard]] Status
{
public:
    constexpr Status() : code_(ENGINE_OK) {}
    constexpr Status(engine_status code) : code_(code) {}

    constexpr bool Ok() const { return code_ == ENGINE_OK; }
    constexpr explicit operator bool() const { return code_ == ENGINE_OK; }
    constexpr engine_status Code() const { return code_; }
    constexpr bool operator==(engine_status other) const { return code_ == other; }
    constexpr bool operator!=(engine_status other) const { return code_ != other; }

    /// The status's name (`ok`, `stale_handle`, ...), static, never empty.
    engine_string Name(const engine_api* api) const { return api->status_name(code_); }

private:
    engine_status code_;
};

/// A zeroed struct with its `struct_size` set to what this host was compiled with (the rule of the header: initialize
/// with
/// `{0}`, set struct_size, then fill).
template <typename T> inline T Init()
{
    T value = {};
    value.struct_size = static_cast<uint32_t>(sizeof(T));
    return value;
}

/// A UTF-8 string of a NUL-terminated text, or of `length` bytes. Borrowed: the text must outlive the call.
inline engine_string Str(const char* text)
{
    size_t length = 0;
    while (text != nullptr && text[length] != '\0')
    {
        ++length;
    }
    engine_string s;
    s.data = text;
    s.length = length;
    return s;
}

inline engine_string Str(const char* data, size_t length)
{
    engine_string s;
    s.data = data;
    s.length = length;
    return s;
}

/// True when the string and the NUL-terminated text have the same bytes.
inline bool Equals(engine_string a, const char* text)
{
    const engine_string b = Str(text);
    if (a.length != b.length)
    {
        return false;
    }
    for (size_t i = 0; i < a.length; ++i)
    {
        if (a.data[i] != b.data[i])
        {
            return false;
        }
    }
    return true;
}

/// Memory the library produced, released exactly once (the destructor). Move-only. Pass `&buffer.Raw()` to a function
/// that fills an `engine_buffer*`.
class Buffer
{
public:
    Buffer() : raw_() {}
    ~Buffer() { Reset(); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) : raw_(other.raw_) { other.raw_ = engine_buffer(); }
    Buffer& operator=(Buffer&& other)
    {
        if (this != &other)
        {
            Reset();
            raw_ = other.raw_;
            other.raw_ = engine_buffer();
        }
        return *this;
    }

    /// The buffer to hand to a function that fills one. Releases what the object held first, so a reused object leaks
    /// nothing.
    engine_buffer* Fill()
    {
        Reset();
        return &raw_;
    }
    const engine_buffer& Raw() const { return raw_; }

    const char* Data() const { return raw_.data; }
    size_t Size() const { return raw_.size; }
    engine_string Text() const { return Str(raw_.data, raw_.size); }
    bool Equals(const char* text) const { return engine_host::Equals(Text(), text); }

    void Reset() { engine_buffer_release(&raw_); }

private:
    engine_buffer raw_;
};

/// The text of the calling thread's most recent failure (a message for people; never branch on it).
inline Status LastError(const engine_api* api, Buffer* out)
{
    return api->last_error(out->Fill());
}

/// One engine handle that is destroyed when the object goes: the base of every owner. `Destroy` is the member of the
/// table that destroys this kind of object. Move-only; a moved-from object owns nothing.
class Owned
{
public:
    Owned() : api_(nullptr), handle_{0}, destroy_(nullptr) {}
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    Owned(Owned&& other) : api_(other.api_), handle_(other.handle_), destroy_(other.destroy_) { other.Forget(); }
    Owned& operator=(Owned&& other)
    {
        if (this != &other)
        {
            Reset();
            api_ = other.api_;
            handle_ = other.handle_;
            destroy_ = other.destroy_;
            other.Forget();
        }
        return *this;
    }
    ~Owned() { Reset(); }

    explicit operator bool() const { return handle_.value != 0; }
    engine_handle Get() const { return handle_; }
    const engine_api* Api() const { return api_; }

    /// Destroys the object now and says how that went. The object owns nothing afterwards, except after IN_USE (an
    /// engine that still has worlds): it still owns the engine, so destroy the worlds and call `Destroy()` again.
    Status Destroy()
    {
        if (handle_.value == 0)
        {
            return ENGINE_OK;
        }
        const Status status = (api_->*destroy_)(handle_);
        if (status != ENGINE_ERROR_IN_USE)
        {
            Forget();
        }
        return status;
    }

    /// Destroys the object, ignoring the status (what the destructor does).
    void Reset() { (void)Destroy(); }

    /// Gives up ownership without destroying: the handle is the caller's now.
    engine_handle Release()
    {
        const engine_handle handle = handle_;
        Forget();
        return handle;
    }

protected:
    /// For the derived classes' `Create`: adopts a handle a create function returned.
    void Adopt(const engine_api* api, engine_handle handle, engine_status (*engine_api::*destroy)(engine_handle))
    {
        Reset();
        api_ = api;
        handle_ = handle;
        destroy_ = destroy;
    }

private:
    void Forget()
    {
        api_ = nullptr;
        handle_.value = 0;
        destroy_ = nullptr;
    }

    const engine_api* api_;
    engine_handle handle_;
    engine_status (*engine_api::*destroy_)(engine_handle);
};

class World;
class Counter;

/// The engine. At most one exists per process (the C API's rule).
class Engine : public Owned
{
public:
    /// Creates the engine. On failure `*out` is left as it was and the status says why (the message: `LastError`).
    static Status Create(const engine_api* api, const engine_create_info& info, Engine* out)
    {
        engine_handle handle = {0};
        const Status status = api->create(&info, &handle);
        if (status)
        {
            out->Adopt(api, handle, &engine_api::destroy);
        }
        return status;
    }

    Status Info(engine_info* out) const
    {
        *out = Init<engine_info>();
        return Api()->get_info(Get(), out);
    }
    Status SettingSet(const char* key, const char* value) const
    {
        return Api()->setting_set(Get(), Str(key), Str(value));
    }
    Status SettingGet(const char* key, Buffer* out) const { return Api()->setting_get(Get(), Str(key), out->Fill()); }

    /// Runs one frame (every world's systems once). `stats` may be null.
    Status Frame(const engine_frame_info& info, engine_frame_stats* stats = nullptr) const
    {
        if (stats != nullptr)
        {
            *stats = Init<engine_frame_stats>();
        }
        return Api()->frame(Get(), &info, stats);
    }

    inline Status CreateWorld(World* out) const;
    inline Status CreateCounter(Counter* out) const;
};

/// A world: entities and their components.
class World : public Owned
{
public:
    Status Entity(uint64_t* out_key) const { return Api()->entity_create(Get(), out_key); }
    Status DestroyEntity(uint64_t key) const { return Api()->entity_destroy(Get(), key); }

private:
    friend class Engine;
};

/// A counter of the job system.
class Counter : public Owned
{
private:
    friend class Engine;
};

inline Status Engine::CreateWorld(World* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->world_create(Get(), &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::world_destroy);
    }
    return status;
}

inline Status Engine::CreateCounter(Counter* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->counter_create(Get(), &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::counter_destroy);
    }
    return status;
}

}  // namespace engine_host

#endif
