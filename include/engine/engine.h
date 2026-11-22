// engine.h: a thin C++ layer over the C API (engine_c.h), for hosts written in C++. Header-only, no exceptions, no
// RTTI, no standard library: it includes engine_c.h and nothing else, so a host copies engine_c.h, engine_abi_version.h
// and this file.
//
// It adds three things the C header cannot enforce and nothing else: ownership (a handle is destroyed exactly once, a
// buffer released exactly once), struct sizing (`Init<T>()` zeroes a struct and sets `struct_size`), and a status that
// cannot be dropped silently by accident (`[[nodiscard]]`). It adds no ABI: every call is the table's function, called
// through the pointer you give it, and the C header stays the contract. Every function of the table has a member or a
// free function here (the ctest `engine_h_covers_table` fails when one does not), named for what it does; the
// arguments are the table's, minus the handle the object holds. `handle.Get()` still gives the raw handle.
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
class Resource;
class Ticket;
class ImportTicket;
class AddTicket;
class Surface;
class PixelsTicket;
class Texture;
class ImportResult;

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

    /// Installs and removes the engine's crash handler (`crash_handler_install`, `crash_handler_uninstall`).
    Status InstallCrashHandler() const { return Api()->crash_handler_install(Get()); }
    Status UninstallCrashHandler() const { return Api()->crash_handler_uninstall(Get()); }

    /// Submits `fn(user)` to the job system; `counter` counts it until it has run (`jobs_submit`).
    inline Status SubmitJob(engine_job_fn fn, void* user, const Counter& counter) const;

    /// The resource registry: mounting, replacing and removing resources, asking what is mounted, loading one now or in
    /// the background (`resource_*`). A replaced or removed resource's dependents come back in `dependents` (may be
    /// null).
    Status AddResource(const engine_resource_add_info& info) const { return Api()->resource_add(Get(), &info); }
    Status ReplaceResource(const engine_resource_add_info& info, Buffer* dependents = nullptr) const
    {
        return Api()->resource_replace(Get(), &info, dependents != nullptr ? dependents->Fill() : nullptr);
    }
    Status RemoveResource(uint64_t id, uint32_t flags, Buffer* dependents = nullptr) const
    {
        return Api()->resource_remove(Get(), id, flags, dependents != nullptr ? dependents->Fill() : nullptr);
    }
    Status MountedResource(uint64_t id, engine_resource_info* out) const
    {
        *out = Init<engine_resource_info>();
        return Api()->resource_mounted(Get(), id, out);
    }
    inline Status LoadResource(uint64_t id, const char* path, uint32_t type, Resource* out) const;
    inline Status LoadResourceAsync(uint64_t id, const char* path, uint32_t type, uint32_t priority, Ticket* out) const;

    /// The host's cache of imported resources (maintenance, 2026-11-18): the blob of a resource of the memory backend
    /// (`resource_read`; `max_size` 0 is no limit), many blobs added all or none, at once or in the background
    /// (`resource_add_many`, `resource_add_many_async`; `failed_index` may be null), and the fingerprint of what
    /// imports write (`import_format_version`, which needs no engine: this static form takes the table).
    Status ReadResource(uint64_t id, uint64_t max_size, Buffer* out) const
    {
        return Api()->resource_read(Get(), id, max_size, out->Fill());
    }
    Status AddResources(const engine_resource_add_info* infos, size_t count, size_t* failed_index = nullptr) const
    {
        return Api()->resource_add_many(Get(), infos, count, failed_index);
    }
    inline Status AddResourcesAsync(const engine_resource_add_info* infos, size_t count, AddTicket* out) const;
    static uint64_t ImportFormatVersion(const engine_api* api) { return api->import_format_version(); }

    /// Imports a glTF file held in memory, at once or in the background (`import_gltf`, `import_gltf_async`).
    Status ImportGltf(const void* bytes, size_t size, const engine_import_options& options,
                      engine_import_result* result) const
    {
        *result = Init<engine_import_result>();
        return Api()->import_gltf(Get(), bytes, size, &options, result);
    }
    inline Status ImportGltf(const void* bytes, size_t size, const engine_import_options& options,
                             ImportResult* result) const;
    inline Status ImportGltfAsync(const void* bytes, size_t size, const engine_import_options& options,
                                  ImportTicket* out) const;

    /// A surface in a host's window, a host texture (`surface_create`, `texture_create`).
    inline Status CreateSurface(const engine_surface_info& info, Surface* out) const;
    inline Status CreateTexture(const engine_texture_info& info, Texture* out) const;

    /// Sets the font the label component draws with, from the bytes of a font file (`label_font_set`).
    Status SetLabelFont(const void* data, size_t size) const { return Api()->label_font_set(Get(), data, size); }

    /// The host-driven audio (`audio_open` and the rest): clips, voices, buses, the render call and the events.
    Status AudioOpen(const engine_audio_info& info) const { return Api()->audio_open(Get(), &info); }
    Status AudioClose() const { return Api()->audio_close(Get()); }
    Status AudioClipCreate(const engine_audio_clip_info& info, const void* data, size_t size, uint64_t* out) const
    {
        return Api()->audio_clip_create(Get(), &info, data, size, out);
    }
    Status AudioClipDestroy(uint64_t clip) const { return Api()->audio_clip_destroy(Get(), clip); }
    Status AudioPlay(const engine_audio_play& desc, uint64_t* out_voice) const
    {
        return Api()->audio_play(Get(), &desc, out_voice);
    }
    Status AudioVoiceSet(uint64_t voice, const engine_audio_voice_params& params) const
    {
        return Api()->audio_voice_set(Get(), voice, &params);
    }
    Status AudioVoiceStop(uint64_t voice, uint32_t fade_frames = 0) const
    {
        return Api()->audio_voice_stop(Get(), voice, fade_frames);
    }
    Status AudioBusCreate(engine_string name, uint32_t parent, uint32_t* out_bus) const
    {
        return Api()->audio_bus_create(Get(), name, parent, out_bus);
    }
    Status AudioBusSet(uint32_t bus, const engine_audio_bus_params& params) const
    {
        return Api()->audio_bus_set(Get(), bus, &params);
    }
    Status AudioRender(uint32_t frames, float* out) const { return Api()->audio_render(Get(), frames, out); }
    Status AudioEvents(engine_audio_event* out, size_t capacity, size_t* total) const
    {
        return Api()->audio_events(Get(), out, capacity, total);
    }
    Status AudioState(engine_audio_state* out) const { return Api()->audio_state(Get(), out); }

    /// Streams, music, effects and snapshots of the host-driven audio.
    Status AudioStreamOpenClip(uint64_t clip, uint64_t* out) const
    {
        return Api()->audio_stream_open_clip(Get(), clip, out);
    }
    Status AudioStreamOpen(const engine_audio_stream_source& source, uint64_t* out) const
    {
        return Api()->audio_stream_open(Get(), &source, out);
    }
    Status AudioStreamClose(uint64_t stream) const { return Api()->audio_stream_close(Get(), stream); }
    /// Live voices (maintenance, 2026-11-18): a stream the host feeds as its audio arrives (`audio_stream_open_push`),
    /// pushed to from the thread the audio comes on (`audio_stream_push`, `audio_stream_push_i16`; `accepted` may be
    /// null), and its counters (`audio_stream_stats`). Set it as the `stream` of an audio source to place it in the
    /// world.
    Status AudioStreamOpenPush(const engine_audio_push_info& info, uint64_t* out) const
    {
        return Api()->audio_stream_open_push(Get(), &info, out);
    }
    Status AudioStreamPush(uint64_t stream, const float* data, uint32_t frames, uint32_t* accepted = nullptr) const
    {
        return Api()->audio_stream_push(Get(), stream, data, frames, accepted);
    }
    Status AudioStreamPushI16(uint64_t stream, const int16_t* data, uint32_t frames, uint32_t* accepted = nullptr) const
    {
        return Api()->audio_stream_push_i16(Get(), stream, data, frames, accepted);
    }
    Status AudioStreamStats(uint64_t stream, engine_audio_stream_stats* out) const
    {
        return Api()->audio_stream_stats(Get(), stream, out);
    }
    Status AudioStreamPlay(uint64_t stream, const engine_audio_play& desc, uint64_t* out_voice) const
    {
        return Api()->audio_stream_play(Get(), stream, &desc, out_voice);
    }
    Status AudioUpdate(bool wait = false) const { return Api()->audio_update(Get(), wait ? 1u : 0u); }
    Status AudioMusicPlay(uint64_t stream, uint32_t bus, uint32_t fade_frames,
                          const engine_audio_tempo* tempo = nullptr) const
    {
        return Api()->audio_music_play(Get(), stream, bus, fade_frames, tempo);
    }
    Status AudioMusicCrossfade(uint64_t stream, uint32_t fade_frames, bool at_bar,
                               const engine_audio_tempo* tempo = nullptr) const
    {
        return Api()->audio_music_crossfade(Get(), stream, fade_frames, at_bar ? 1u : 0u, tempo);
    }
    Status AudioMusicStop(uint32_t fade_frames) const { return Api()->audio_music_stop(Get(), fade_frames); }
    Status AudioMusicLayers(const uint64_t* streams, const float* gains, uint32_t count, uint32_t bus) const
    {
        return Api()->audio_music_layers(Get(), streams, gains, count, bus);
    }
    Status AudioMusicLayerGain(uint32_t layer, float gain, uint32_t frames) const
    {
        return Api()->audio_music_layer_gain(Get(), layer, gain, frames);
    }
    Status AudioMusicStinger(uint64_t clip, float gain, bool on_beat, uint64_t* out_frame = nullptr) const
    {
        return Api()->audio_music_stinger(Get(), clip, gain, on_beat ? 1u : 0u, out_frame);
    }
    Status AudioBusEffectSet(uint32_t bus, uint32_t slot, const engine_audio_effect& effect) const
    {
        return Api()->audio_bus_effect_set(Get(), bus, slot, &effect);
    }
    Status AudioBusSend(uint32_t bus, uint32_t slot, uint32_t aux, float gain, bool pre_fader = false) const
    {
        return Api()->audio_bus_send(Get(), bus, slot, aux, gain, pre_fader ? 1u : 0u);
    }
    Status AudioBusDuck(uint32_t target, uint32_t source, float depth, float threshold, float release_frames) const
    {
        return Api()->audio_bus_duck(Get(), target, source, depth, threshold, release_frames);
    }
    Status AudioSnapshot(Buffer* out) const { return Api()->audio_snapshot(Get(), out->Fill()); }
    Status AudioRestore(const void* data, size_t size) const { return Api()->audio_restore(Get(), data, size); }
};

/// A world: entities and their components.
class World : public Owned
{
public:
    Status Entity(uint64_t* out_key) const { return Api()->entity_create(Get(), out_key); }
    Status DestroyEntity(uint64_t key) const { return Api()->entity_destroy(Get(), key); }

    /// Components: `value` and `out` are the struct of `kind` (`ENGINE_COMPONENT_*`), with `struct_size` set.
    Status SetComponent(uint64_t key, uint32_t kind, const void* value) const
    {
        return Api()->component_set(Get(), key, kind, value);
    }
    Status GetComponent(uint64_t key, uint32_t kind, void* out) const
    {
        return Api()->component_get(Get(), key, kind, out);
    }
    Status RemoveComponent(uint64_t key, uint32_t kind) const { return Api()->component_remove(Get(), key, kind); }

    /// Scripts (`ENGINE_CAP_SCRIPT`): `ScriptLoad` adds a script to the world, `ScriptCall` calls a
    /// global function with a value pool in and out, `ScriptRegisterFunction` makes a host function callable from
    /// scripts (before the first script), `ScriptSetBudget` limits calls, `ScriptStats` reads the counters. Script
    /// components are `ENGINE_COMPONENT_SCRIPT` with `SetComponent`.
    Status ScriptLoad(const engine_script_load& info, uint64_t* out_script = nullptr) const
    {
        return Api()->script_load(Get(), &info, out_script);
    }
    Status ScriptCall(engine_string function, const engine_value_pool* args, engine_value_pool* results) const
    {
        return Api()->script_call(Get(), function, args, results);
    }
    Status ScriptRegisterFunction(engine_string name, uint32_t arity, uint32_t flags, engine_script_fn function,
                                  void* user) const
    {
        return Api()->script_register_function(Get(), name, arity, flags, function, user);
    }
    Status ScriptSetBudget(const engine_script_budget& budget) const
    {
        return Api()->script_set_budget(Get(), &budget);
    }
    Status ScriptStats(engine_script_stats* out) const { return Api()->script_stats(Get(), out); }
    Status ScriptEventEmit(engine_string name, const engine_value_pool* payload, uint64_t target) const
    {
        return Api()->script_event_emit(Get(), name, payload, target);
    }
    Status ScriptEventSubscribe(engine_string name, engine_script_event_fn callback, void* user, uint32_t* out_id) const
    {
        return Api()->script_event_subscribe(Get(), name, callback, user, out_id);
    }
    Status ScriptEventUnsubscribe(uint32_t id) const { return Api()->script_event_unsubscribe(Get(), id); }
    Status ScriptTaskRun(const engine_script_task& task, engine_value_pool* results) const
    {
        return Api()->script_task_run(Get(), &task, results);
    }
    Status HasComponent(uint64_t key, uint32_t kind, bool* out) const
    {
        int has = 0;
        const Status status = Api()->component_has(Get(), key, kind, &has);
        *out = has != 0;
        return status;
    }

    /// Names, parents and lookups (`entity_name_*`, `entity_*_parent`, `entity_find*`, `query`). The key-list calls
    /// return the total in `total` even when `capacity` is smaller (ask with a capacity of 0 for the count).
    Status SetName(uint64_t key, const char* name) const { return Api()->entity_name_set(Get(), key, Str(name)); }
    Status GetName(uint64_t key, Buffer* out) const { return Api()->entity_name_get(Get(), key, out->Fill()); }
    Status Query(uint32_t mask, uint64_t* keys, size_t capacity, size_t* total) const
    {
        return Api()->query(Get(), mask, keys, capacity, total);
    }
    Status SetParent(uint64_t child, uint64_t parent) const { return Api()->entity_set_parent(Get(), child, parent); }
    Status GetParent(uint64_t key, uint64_t* parent) const { return Api()->entity_get_parent(Get(), key, parent); }
    Status Find(const char* name, uint64_t* key) const { return Api()->entity_find(Get(), Str(name), key); }
    Status FindAll(const char* name, uint64_t* keys, size_t capacity, size_t* total) const
    {
        return Api()->entity_find_all(Get(), Str(name), keys, capacity, total);
    }

    /// The environment (sky, ambient light), material slot overrides and level spawning (`world_set_environment`,
    /// `entity_*_slot_override`, `level_spawn`).
    Status SetEnvironment(const engine_environment& environment) const
    {
        return Api()->world_set_environment(Get(), &environment);
    }
    Status SetSlotOverride(uint64_t key, const char* slot, const engine_slot_override& value) const
    {
        return Api()->entity_set_slot_override(Get(), key, Str(slot), &value);
    }
    /// An override of one mesh slot only (`entity_set_mesh_slot_override`).
    Status SetMeshSlotOverride(uint64_t key, const char* mesh_slot, const char* slot,
                               const engine_slot_override& value) const
    {
        return Api()->entity_set_mesh_slot_override(Get(), key, Str(mesh_slot), Str(slot), &value);
    }
    Status ClearSlotOverride(uint64_t key, const char* slot) const
    {
        return Api()->entity_clear_slot_override(Get(), key, Str(slot));
    }
    /// A material per mesh slot (`entity_set_mesh_slot_material`, `..._by_name`, `entity_clear_mesh_slot_materials`).
    Status SetMeshSlotMaterial(uint64_t key, uint32_t slot, uint64_t material_resource) const
    {
        return Api()->entity_set_mesh_slot_material(Get(), key, slot, material_resource);
    }
    Status SetMeshSlotMaterial(uint64_t key, const char* slot, uint64_t material_resource) const
    {
        return Api()->entity_set_mesh_slot_material_by_name(Get(), key, Str(slot), material_resource);
    }
    Status ClearMeshSlotMaterials(uint64_t key) const { return Api()->entity_clear_mesh_slot_materials(Get(), key); }
    Status SpawnLevel(uint64_t level, const engine_spawn_params& params, uint64_t* root, uint64_t* keys,
                      size_t capacity, size_t* total) const
    {
        return Api()->level_spawn(Get(), level, &params, root, keys, capacity, total);
    }

    /// Animation and skeletons (`animation_*`, `skeleton_*`, `entity_attach_to_joint`).
    Status PlayAnimation(uint64_t key, uint64_t clip, const engine_play_params& params) const
    {
        return Api()->animation_play(Get(), key, clip, &params);
    }
    Status SetAnimationSpeed(uint64_t key, float speed) const { return Api()->animation_set_speed(Get(), key, speed); }
    Status StopAnimation(uint64_t key) const { return Api()->animation_stop(Get(), key); }
    Status AnimationTime(uint64_t key, double* seconds) const { return Api()->animation_time(Get(), key, seconds); }
    Status FindJoint(uint64_t key, const char* name, uint32_t* index) const
    {
        return Api()->skeleton_find_joint(Get(), key, Str(name), index);
    }
    Status JointTransform(uint64_t key, uint32_t joint, float* matrix) const
    {
        return Api()->skeleton_joint_transform(Get(), key, joint, matrix);
    }
    Status AttachToJoint(uint64_t key, uint64_t skeleton_key, uint32_t joint) const
    {
        return Api()->entity_attach_to_joint(Get(), key, skeleton_key, joint);
    }

    /// Physics (`physics_*`, `body_*`, `joint_reaction`, `character_*`). Vectors are 3 floats, rotations 4 (x, y, z,
    /// w). A world has one set of characters, shared with its scripts (2026-11-20): one made by
    /// ENGINE_COMPONENT_CHARACTER is the one a behavior's `character.*` sees, and one a script made is driven and read
    /// here. They move inside the fixed physics steps; after a frame the entity's transform takes the capsule's centre,
    /// interpolated between the last two steps when `physics.interpolate` is on.
    Status SetGravity(const float* gravity) const { return Api()->physics_set_gravity(Get(), gravity); }
    Status ApplyForce(uint64_t key, const float* force, const float* point) const
    {
        return Api()->body_apply_force(Get(), key, force, point);
    }
    Status ApplyImpulse(uint64_t key, const float* impulse, const float* point) const
    {
        return Api()->body_apply_impulse(Get(), key, impulse, point);
    }
    Status SetVelocity(uint64_t key, const float* linear, const float* angular) const
    {
        return Api()->body_set_velocity(Get(), key, linear, angular);
    }
    Status GetVelocity(uint64_t key, float* linear, float* angular) const
    {
        return Api()->body_get_velocity(Get(), key, linear, angular);
    }
    Status Teleport(uint64_t key, const float* position, const float* rotation) const
    {
        return Api()->body_teleport(Get(), key, position, rotation);
    }
    Status RayCast(const float* origin, const float* direction, float max_distance, uint32_t layer_mask,
                   engine_ray_hit* hit, bool* found) const
    {
        *hit = Init<engine_ray_hit>();
        int any = 0;
        const Status status = Api()->physics_raycast(Get(), origin, direction, max_distance, layer_mask, hit, &any);
        *found = any != 0;
        return status;
    }
    Status OverlapSphere(const float* center, float radius, uint32_t layer_mask, uint64_t* keys, size_t capacity,
                         size_t* total) const
    {
        return Api()->physics_overlap_sphere(Get(), center, radius, layer_mask, keys, capacity, total);
    }
    Status OverlapBox(const float* center, const float* half_extents, uint32_t layer_mask, uint64_t* keys,
                      size_t capacity, size_t* total) const
    {
        return Api()->physics_overlap_box(Get(), center, half_extents, layer_mask, keys, capacity, total);
    }
    Status JointReaction(uint64_t key, float* force, float* torque) const
    {
        return Api()->joint_reaction(Get(), key, force, torque);
    }
    /// Sets the velocity the character walks at (`character_move`): it holds until set again and each fixed step
    /// carries it out, so a frame without a step moves nothing and the distance per second does not depend on the frame
    /// rate. Not a one-shot move: set a zero velocity to stop. `dt` is checked (in (0, 1]) and otherwise unused.
    /// `state` receives the last step's state.
    Status MoveCharacter(uint64_t key, const float* velocity, float dt, engine_character_state* state) const
    {
        *state = Init<engine_character_state>();
        return Api()->character_move(Get(), key, velocity, dt, state);
    }
    /// Asks for a jump (`character_jump`) when the character stands: it starts at the next fixed step. A `speed` of 0
    /// only reads the last step's state.
    Status JumpCharacter(uint64_t key, float speed, engine_character_state* state) const
    {
        *state = Init<engine_character_state>();
        return Api()->character_jump(Get(), key, speed, state);
    }
    /// Copies the events of the last frame to `out[0..capacity)`; every element's `struct_size` is set here (the call
    /// refuses an element without it). `total` is the count of events, which may exceed `capacity`.
    Status PhysicsEvents(engine_physics_event* out, size_t capacity, size_t* total) const
    {
        for (size_t i = 0; i < capacity; ++i)
        {
            out[i] = Init<engine_physics_event>();
        }
        return Api()->physics_events(Get(), out, capacity, total);
    }
    /// Writes the world's physics, its characters' state (vertical speed, ground, held move) included, into `out`;
    /// `PhysicsRestore` puts it back.
    Status PhysicsSnapshot(Buffer* out) const { return Api()->physics_snapshot(Get(), out->Fill()); }
    Status PhysicsRestore(const void* data, size_t size) const { return Api()->physics_restore(Get(), data, size); }

private:
    friend class Engine;
};

/// A counter of the job system.
class Counter : public Owned
{
public:
    /// The number of jobs submitted against the counter that have not finished (`counter_value`).
    Status Value(uint64_t* out) const { return Api()->counter_value(Get(), out); }
    /// Waits until the counter is zero or `timeout_ms` passed (`counter_wait`; TIMEOUT when it did not reach zero).
    Status Wait(uint64_t timeout_ms) const { return Api()->counter_wait(Get(), timeout_ms); }

private:
    friend class Engine;
};

/// A mounted resource loaded into memory; releasing it (the destructor) gives the reference back (`resource_release`).
class Resource : public Owned
{
public:
    Status Info(engine_resource_info* out) const
    {
        *out = Init<engine_resource_info>();
        return Api()->resource_status(Get(), out);
    }

private:
    friend class Engine;
    friend class Ticket;
};

/// A background resource load (`resource_load_async`); destroying it (the destructor) cancels nothing that finished.
class Ticket : public Owned
{
public:
    Status Poll(engine_ticket_info* out) const
    {
        *out = Init<engine_ticket_info>();
        return Api()->ticket_poll(Get(), out);
    }
    Status Wait(uint64_t timeout_ms) const { return Api()->ticket_wait(Get(), timeout_ms); }
    /// The loaded resource, owned by `out` from now on.
    inline Status Finish(Resource* out) const;
    Status Cancel() const { return Api()->ticket_cancel(Get()); }

private:
    friend class Engine;
};

/// What a glTF import made; its six buffers (ids, types, paths, warnings, manifest, summary) are released with the
/// object.
class ImportResult
{
public:
    ImportResult() : raw_(Init<engine_import_result>()) {}
    ~ImportResult() { Reset(); }
    ImportResult(const ImportResult&) = delete;
    ImportResult& operator=(const ImportResult&) = delete;

    /// The structure to hand to a function that fills one. Releases what the object held first.
    engine_import_result* Fill()
    {
        Reset();
        raw_ = Init<engine_import_result>();
        return &raw_;
    }
    const engine_import_result& Raw() const { return raw_; }

    void Reset()
    {
        engine_buffer_release(&raw_.ids);
        engine_buffer_release(&raw_.types);
        engine_buffer_release(&raw_.paths);
        engine_buffer_release(&raw_.warnings);
        engine_buffer_release(&raw_.manifest);
        engine_buffer_release(&raw_.summary);
        raw_ = Init<engine_import_result>();
    }

private:
    engine_import_result raw_;
};

/// A background glTF import (`import_gltf_async`).
class ImportTicket : public Owned
{
public:
    Status Poll(engine_import_info* out) const
    {
        *out = Init<engine_import_info>();
        return Api()->import_poll(Get(), out);
    }
    Status Wait(uint64_t timeout_ms) const { return Api()->import_wait(Get(), timeout_ms); }
    Status Finish(ImportResult* out) const { return Api()->import_finish(Get(), out->Fill()); }
    Status Cancel() const { return Api()->import_cancel(Get()); }

private:
    friend class Engine;
};

/// An adding of many resources in the background (`resource_add_many_async`). The bytes of the items stay the host's
/// and must stay valid until `Finish` or the destruction of the ticket.
class AddTicket : public Owned
{
public:
    /// Waits until the adding is done, at most `timeout_ms` (0 only looks): OK when done, NOT_READY on a timeout.
    Status Wait(uint64_t timeout_ms) const { return Api()->resource_add_wait(Get(), timeout_ms); }
    /// Takes the outcome of a finished adding; the ticket is gone after it (except for NOT_READY and IN_USE).
    Status Finish(size_t* failed_index = nullptr)
    {
        const Status status = Api()->resource_add_finish(Get(), failed_index);
        if (status != ENGINE_ERROR_NOT_READY && status != ENGINE_ERROR_IN_USE)
        {
            (void)Release();
        }
        return status;
    }

private:
    friend class Engine;
};

/// The pixels of a surface read back (`surface_read_pixels`, then `pixels_finish`); released with the object.
class Pixels
{
public:
    Pixels() : raw_(Init<engine_pixels>()) {}
    ~Pixels() { Reset(); }
    Pixels(const Pixels&) = delete;
    Pixels& operator=(const Pixels&) = delete;

    engine_pixels* Fill()
    {
        Reset();
        raw_ = Init<engine_pixels>();
        return &raw_;
    }
    const engine_pixels& Raw() const { return raw_; }
    void Reset()
    {
        engine_buffer_release(&raw_.data);
        raw_ = Init<engine_pixels>();
    }

private:
    engine_pixels raw_;
};

/// A pending read of a surface's pixels.
class PixelsTicket : public Owned
{
public:
    Status Finish(Pixels* out) const { return Api()->pixels_finish(Get(), out->Fill()); }

private:
    friend class Surface;
};

/// A surface in a host's window: the engine draws a world into it (`surface_*`).
class Surface : public Owned
{
public:
    Status Input(const engine_input_event& event) const { return Api()->surface_input(Get(), &event); }
    Status Resized(uint32_t width, uint32_t height, float scale) const
    {
        return Api()->surface_resized(Get(), width, height, scale);
    }
    Status Rebind(const engine_surface_info& info) const { return Api()->surface_rebind(Get(), &info); }
    Status SetBackground(const float* rgba) const { return Api()->surface_set_background(Get(), rgba); }
    Status State(engine_surface_state* out) const
    {
        *out = Init<engine_surface_state>();
        return Api()->surface_state(Get(), out);
    }
    inline Status ReadPixels(PixelsTicket* out) const;
    inline Status SetWorld(const World& world) const;
    Status SetCamera(uint64_t camera_entity) const { return Api()->surface_set_camera(Get(), camera_entity); }
    Status PixelRay(float x, float y, float* origin, float* direction) const
    {
        return Api()->surface_pixel_ray(Get(), x, y, origin, direction);
    }
    Status Project(const float* point, float* out) const { return Api()->surface_project(Get(), point, out); }
    /// The newest finished frame of an off-screen surface (`surface_acquire_frame`); give it back with
    /// [[ReleaseFrame]].
    Status AcquireFrame(engine_frame_image* out) const
    {
        *out = Init<engine_frame_image>();
        return Api()->surface_acquire_frame(Get(), out);
    }
    /// Gives a frame taken with [[AcquireFrame]] back (`surface_release_frame`).
    Status ReleaseFrame(const engine_frame_image& image) const { return Api()->surface_release_frame(Get(), &image); }

private:
    friend class Engine;
};

/// A host texture the host fills and the scene draws (`texture_*`).
class Texture : public Owned
{
public:
    Status Update(const void* pixels, size_t stride, const engine_update_info& info) const
    {
        return Api()->texture_update(Get(), pixels, stride, &info);
    }
    Status Resize(uint32_t width, uint32_t height) const { return Api()->texture_resize(Get(), width, height); }
    Status ReadPixels(Buffer* out, uint32_t* width, uint32_t* height) const
    {
        return Api()->texture_read_pixels(Get(), out->Fill(), width, height);
    }

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

inline Status Engine::SubmitJob(engine_job_fn fn, void* user, const Counter& counter) const
{
    return Api()->jobs_submit(Get(), fn, user, counter.Get());
}

inline Status Engine::LoadResource(uint64_t id, const char* path, uint32_t type, Resource* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->resource_load(Get(), id, Str(path), type, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::resource_release);
    }
    return status;
}

inline Status Engine::LoadResourceAsync(uint64_t id, const char* path, uint32_t type, uint32_t priority,
                                        Ticket* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->resource_load_async(Get(), id, Str(path), type, priority, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::ticket_destroy);
    }
    return status;
}

inline Status Ticket::Finish(Resource* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->ticket_finish(Get(), &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::resource_release);
    }
    return status;
}

inline Status Engine::ImportGltf(const void* bytes, size_t size, const engine_import_options& options,
                                 ImportResult* result) const
{
    return Api()->import_gltf(Get(), bytes, size, &options, result->Fill());
}

inline Status Engine::ImportGltfAsync(const void* bytes, size_t size, const engine_import_options& options,
                                      ImportTicket* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->import_gltf_async(Get(), bytes, size, &options, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::import_destroy);
    }
    return status;
}

inline Status Engine::AddResourcesAsync(const engine_resource_add_info* infos, size_t count, AddTicket* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->resource_add_many_async(Get(), infos, count, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::resource_add_destroy);
    }
    return status;
}

inline Status Engine::CreateSurface(const engine_surface_info& info, Surface* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->surface_create(Get(), &info, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::surface_destroy);
    }
    return status;
}

inline Status Engine::CreateTexture(const engine_texture_info& info, Texture* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->texture_create(Get(), &info, &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::texture_destroy);
    }
    return status;
}

inline Status Surface::ReadPixels(PixelsTicket* out) const
{
    engine_handle handle = {0};
    const Status status = Api()->surface_read_pixels(Get(), &handle);
    if (status)
    {
        out->Adopt(Api(), handle, &engine_api::pixels_destroy);
    }
    return status;
}

inline Status Surface::SetWorld(const World& world) const
{
    return Api()->surface_set_world(Get(), world.Get());
}

/// The names the layer does not wrap by object: a free function each, with the table you give it.
inline const void* GetExtension(const engine_api* api, const char* name)
{
    return api->get_extension(Str(name));
}
/// Tells the engine the calling thread is done with it (`thread_detach`); call it before a host thread exits.
inline void ThreadDetach(const engine_api* api)
{
    api->thread_detach();
}

}  // namespace engine_host

#endif
