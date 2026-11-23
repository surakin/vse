#include "engine_c/surface.h"

#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "engine_c/surface_scene.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "os/os.h"
#include "platform/foreign_window.h"
#include "rhi/backends/backends.h"
#include "rhi/command_list.h"
#include "rhi/device.h"
#include "rhi/rhi.h"
#include "rhi/swapchain.h"

#include "build_defs.h"
#if ENGINE_PLATFORM_MACOS
#include "engine_c/macos/cocoa_surface.h"
#endif

#define TRY_SURFACE(expression)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status try_surface_status = (expression);                                                         \
        if (try_surface_status != ENGINE_OK)                                                                           \
        {                                                                                                              \
            return try_surface_status;                                                                                 \
        }                                                                                                              \
    } while (0)

namespace vse::engine_c
{

namespace
{
constexpr usize kMaxSurfaces = 64;
constexpr usize kMaxPixelTickets = 256;
constexpr u32 kMaxDimension = 16384;
constexpr usize kNoTicket = ~usize{0};
}  // namespace

// A read-back request: it waits for the next frame of its surface.
struct PixelsObject
{
    enum class State : u8
    {
        Waiting,
        Done,
        Failed,
        Taken
    };
    State state = State::Waiting;
    engine_status failure = ENGINE_OK;
    u32 width = 0;
    u32 height = 0;
    Vector<byte> rgba;
    u64 surface_generation = 0;  // the surface it waits on
};

// Answers the engine's requests on a surface with the host's callbacks.
class CallbackHooks final : public os::ForeignWindowHooks
{
public:
    void Set(const engine_surface_callbacks& callbacks) { callbacks_ = callbacks; }
    bool SupportsCursor() const override { return callbacks_.set_cursor != nullptr; }
    bool SetCursor(os::CursorShape shape) override
    {
        return callbacks_.set_cursor != nullptr && callbacks_.set_cursor(callbacks_.user, static_cast<u32>(shape)) != 0;
    }
    bool SupportsPointerLock() const override { return callbacks_.set_pointer_lock != nullptr; }
    bool SetPointerLock(bool locked) override
    {
        return callbacks_.set_pointer_lock != nullptr &&
               callbacks_.set_pointer_lock(callbacks_.user, locked ? 1 : 0) != 0;
    }
    bool SupportsClipboard() const override
    {
        return callbacks_.clipboard_get != nullptr || callbacks_.clipboard_set != nullptr;
    }
    bool SetClipboardText(StringView text) override
    {
        engine_string s;
        s.data = text.Data();
        s.length = text.Length();
        return callbacks_.clipboard_set != nullptr && callbacks_.clipboard_set(callbacks_.user, s) != 0;
    }
    bool GetClipboardText(Vector<byte>* out) override
    {
        out->Clear();
        engine_string s = {};
        if (callbacks_.clipboard_get == nullptr || callbacks_.clipboard_get(callbacks_.user, &s) == 0 ||
            s.data == nullptr)
        {
            return false;
        }
        // One byte past the limit is enough for the caller to see "too large" without copying all of a huge text.
        const usize copied = s.length > os::kMaxClipboardBytes ? os::kMaxClipboardBytes + 1 : s.length;
        out->Resize(copied);
        MemCopy(out->Data(), s.data, copied);
        return true;
    }
    void SetTextInputRect(i32 x, i32 y, i32 width, i32 height) override
    {
        if (callbacks_.text_input_rect != nullptr)
        {
            callbacks_.text_input_rect(callbacks_.user, x, y, width, height);
        }
    }
    void RequestFocus(bool focused) override
    {
        if (callbacks_.request_focus != nullptr)
        {
            callbacks_.request_focus(callbacks_.user, focused ? 1 : 0);
        }
    }

private:
    engine_surface_callbacks callbacks_ = {};
};

struct SurfaceObject
{
    SurfaceObject(const os::NativeWindowInfo& info, u32 width, u32 height, f32 scale)
        : window(info, width, height, scale)
    {
        window.SetHooks(&hooks);
    }
#if ENGINE_PLATFORM_MACOS
    // The swapchain is destroyed first (Destroy, ~SurfaceHost); the layer the engine added to the host's view leaves
    // with the surface.
    ~SurfaceObject() { macos::DetachLayer(&cocoa); }
    macos::CocoaLayer cocoa;  // the Metal layer of a Cocoa surface
#endif

    CallbackHooks hooks;  // before the window: the window points at it
    os::ForeignWindow window;
    rhi::DevicePtr device;
    rhi::Swapchain* swapchain = nullptr;
    bool
        offscreen = false;  // ENGINE_SURFACE_OFFSCREEN (Phase 24 Day 378): no window, no swapchain, a target of its own
    rhi::TextureHandle target;  // the colour target of an off-screen surface (RGBA8, sRGB-encoded)
    u32 target_width = 0;
    u32 target_height = 0;
    // The read-back ring of an off-screen surface (Day 379): a copy of the target goes into a GpuToCpu buffer with a
    // fence; a finished one is read out when the host acquires. Held frames are CPU copies of their own.
    struct RingSlot
    {
        rhi::BufferHandle buffer;
        u64 buffer_bytes = 0;
        rhi::FenceValue fence = 0;
        u64 frame = 0;
        u32 width = 0;
        u32 height = 0;
        u32 pitch = 0;
        enum class State : u8
        {
            Free,
            Pending,
            Ready
        } state = State::Free;
    };
    static constexpr u32 kRingSlots = 3;
    static constexpr u32 kMaxHeld = 2;
    RingSlot ring[kRingSlots];
    struct HeldFrame
    {
        bool used = false;
        Vector<byte> rgba;
        u32 width = 0;
        u32 height = 0;
        u64 frame = 0;
    };
    HeldFrame held[kMaxHeld];
    u64 frames_rendered = 0;
    u64 frames_acquired = 0;
    u64 frames_dropped = 0;
    u64 frames_skipped_slow = 0;
    u64 last_acquired_frame = 0;
    f32 background[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    u64 presented = 0;
    u64 recoveries = 0;
    u64 skipped = 0;
    bool visible = false;
    bool lost = false;
    u32 present_mode = 0;
    rhi::Backend backend = rhi::Backend::Vulkan;  // ENGINE_SURFACE_FLAG_*, or the platform's own (Day 354)
    char adapter[rhi::kAdapterIdBytes] = {};      // the adapter of the first device: a recovery asks for it (Day 369)
    engine_handle kept_world = {};  // the world a failed rebind unbound: the next rebind that works binds it again
    u32 active_touch = 0;           // the touch that acts as the pointer, plus one; 0 for none
    Vector<usize> waiting;          // slots of the read-back tickets waiting for a frame
    engine_handle world = {};       // the world bound by surface_set_world (value 0: none)
    engine_status world_status = ENGINE_OK;          // why it was unbound, ENGINE_OK while bound or never bound
    UniquePtr<SurfaceScene> scene;                   // after the swapchain in destruction order: it points at it
    Vector<PixelsObject*>* frame_by_slot = nullptr;  // during RenderAll: the tickets the before-present hook serves
    u32 objects_drawn = 0;
    u64 entities_unresolved = 0;
    u32 lights_refused = 0;
    u32 labels_drawn = 0;
    u32 labels_refused = 0;
    u64 camera_entity = 0;                    // named by surface_set_camera, 0 for the default camera
    engine_status camera_status = ENGINE_OK;  // NOT_FOUND while the named camera is not usable
};

namespace
{

// `layer` is the resolved CAMetalLayer of a Cocoa surface (the host's, the view's own or the engine's sublayer).
os::NativeWindowInfo NativeOf(const engine_surface_info& info, void* layer = nullptr)
{
    os::NativeWindowInfo native;
    if (info.kind == ENGINE_SURFACE_OFFSCREEN)
    {
        native.kind = os::NativeWindowKind::Invalid;  // no native window
    }
    else if (info.kind == ENGINE_SURFACE_COCOA)
    {
        native.kind = os::NativeWindowKind::Cocoa;
        native.display = info.display;
        native.surface = layer;
    }
    else if (info.kind == ENGINE_SURFACE_WAYLAND)
    {
        native.kind = os::NativeWindowKind::Wayland;
        native.display = info.display;
        native.surface = info.surface;
    }
    else if (info.kind == ENGINE_SURFACE_WIN32)
    {
        native.kind = os::NativeWindowKind::Win32;  // display: the HINSTANCE, surface: the HWND
        native.display = info.display;
        native.surface = info.surface;
    }
    else
    {
        native.kind = os::NativeWindowKind::Xlib;
        native.display = info.display;
        native.window_id = info.window_id;
    }
    return native;
}

rhi::Backend BackendOf(const engine_surface_info& info)
{
    if ((info.flags & ENGINE_SURFACE_FLAG_D3D12) != 0)
    {
        return rhi::Backend::Dx12;
    }
    if ((info.flags & ENGINE_SURFACE_FLAG_VULKAN) != 0)
    {
        return rhi::Backend::Vulkan;
    }
    // the platform's own
    if (info.kind == ENGINE_SURFACE_WIN32)
    {
        return rhi::Backend::Dx12;
    }
    if (info.kind == ENGINE_SURFACE_OFFSCREEN)
    {  // the platform's own graphics API
        return ENGINE_PLATFORM_WINDOWS != 0 ? rhi::Backend::Dx12
               : ENGINE_PLATFORM_MACOS != 0 ? rhi::Backend::Metal
                                            : rhi::Backend::Vulkan;
    }
    return info.kind == ENGINE_SURFACE_COCOA ? rhi::Backend::Metal : rhi::Backend::Vulkan;
}

bool BackendSupported(u32 kind, rhi::Backend backend)
{
    if (backend == rhi::Backend::Dx12)
    {
        return ENGINE_PLATFORM_WINDOWS != 0 && (kind == ENGINE_SURFACE_WIN32 || kind == ENGINE_SURFACE_OFFSCREEN);
    }
    if (backend == rhi::Backend::Metal)
    {
        return ENGINE_PLATFORM_MACOS != 0 && (kind == ENGINE_SURFACE_COCOA || kind == ENGINE_SURFACE_OFFSCREEN);
    }
#if ENGINE_RHI_HAVE_VULKAN
    return backend == rhi::Backend::Vulkan && kind != ENGINE_SURFACE_COCOA;  // no Vulkan on macOS (no MoltenVK)
#else
    return false;
#endif
}

engine_status CheckInfo(const engine_surface_info* given, engine_surface_info* out)
{
    if (given == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    if (StructSizeOf(given) < sizeof(engine_surface_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_surface_info.struct_size is smaller than the struct");
    }
    *out = *given;
    if ((out->flags & ~(ENGINE_SURFACE_FLAG_VULKAN | ENGINE_SURFACE_FLAG_D3D12)) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_surface_info.flags has an unknown bit");
    }
    if (out->flags == (ENGINE_SURFACE_FLAG_VULKAN | ENGINE_SURFACE_FLAG_D3D12))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_surface_info.flags asks for two graphics APIs");
    }
    if (!SurfaceKindSupported(out->kind))
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "this build cannot render into a window of that kind");
    }
    if (!BackendSupported(out->kind, BackendOf(*out)))
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED,
                    "this build cannot draw into a window of that kind with that graphics API");
    }
    // Wayland and Win32 name the window by a pointer (`surface`), X11 by an id; a Cocoa surface's CAMetalLayer is
    // optional.
    const bool by_pointer = out->kind == ENGINE_SURFACE_WAYLAND || out->kind == ENGINE_SURFACE_WIN32;
    if (out->kind == ENGINE_SURFACE_OFFSCREEN)
    {  // no window: nothing of the native handles is read
        out->display = nullptr;
        out->surface = nullptr;
        out->window_id = 0;
        out->present_mode = ENGINE_PRESENT_FIFO;
    }
    else if (out->display == nullptr || (by_pointer && out->surface == nullptr) ||
             (out->kind == ENGINE_SURFACE_X11 && out->window_id == 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the native handles of the window are missing");
    }
#if ENGINE_PLATFORM_MACOS
    if (out->kind == ENGINE_SURFACE_COCOA && !macos::OnMainThread())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a Cocoa surface is made and rebound on the main thread");
    }
#endif
    if (out->width > kMaxDimension || out->height > kMaxDimension || !(out->scale >= 0.0f) || !(out->scale <= 64.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the size or the scale is out of range");
    }
    if (out->scale == 0.0f)
    {
        out->scale = 1.0f;
    }
    if (out->present_mode > ENGINE_PRESENT_IMMEDIATE)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_surface_info.present_mode is not an ENGINE_PRESENT_* value");
    }
    if (out->callbacks != nullptr && StructSizeOf(out->callbacks) < sizeof(engine_surface_callbacks))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_surface_callbacks.struct_size is smaller than the struct");
    }
    return ENGINE_OK;
}

void BeforePresent(rhi::TextureHandle image, void* user);
void DestroyRing(SurfaceObject* s, bool destroy_buffers);
void PollRing(SurfaceObject* s);

// Makes the scene a bound world is drawn with, on the surface's device and swapchain.
engine_status MakeScene(SurfaceObject* s, ResourceHost* resources)
{
    s->scene = MakeUnique<SurfaceScene>(s->device.Get(), s->swapchain, resources, &BeforePresent, s);
    if (!s->scene->Valid())
    {
        s->scene.Reset();
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the device cannot draw a scene on this surface");
    }
    Runtime* runtime = CurrentRuntime();
    s->scene->AttachRuntime(runtime);
    return ENGINE_OK;
}

rhi::SwapchainDesc SwapchainDescOf(const SurfaceObject* s)
{
    rhi::SwapchainDesc desc;
    desc.present_mode = s->present_mode == ENGINE_PRESENT_MAILBOX     ? rhi::PresentMode::Mailbox
                        : s->present_mode == ENGINE_PRESENT_IMMEDIATE ? rhi::PresentMode::Immediate
                                                                      : rhi::PresentMode::Fifo;
    return desc;
}

// Makes the device and the swapchain of a surface from its window.
engine_status Build(SurfaceObject* s)
{
    rhi::RegisterCompiledBackends();
    rhi::DeviceCreateInfo info;
    info.backend = s->backend;
    info.application_name = "engine";
    info.window = s->offscreen ? nullptr : &s->window;
    info.pipeline_cache = false;
    // A new device after a loss stays on the surface's adapter: picked anew, a Direct3D 12 adapter whose removed device
    // another surface still holds reads as unsuitable, and the choice would fall to WARP, the software adapter (Day
    // 369). Asked for by id, the creation fails instead, and the host rebinds again later.
    info.adapter = s->adapter[0] != '\0' ? s->adapter : nullptr;
    s->device = rhi::CreateDevice(info);
    if (s->device.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED,
                    s->backend == rhi::Backend::Metal  ? "no Metal device can present to this view"
                    : s->backend == rhi::Backend::Dx12 ? "no Direct3D 12 device can present to this window"
                                                       : "no Vulkan device can present to this window");
    }
    if (s->offscreen)
    {
        CStrCopyTo(s->adapter, sizeof(s->adapter), s->device->Adapter().id);
        return ENGINE_OK;  // the target is made by the first frame that has a size
    }
    s->swapchain = s->device->CreateSwapchain(SwapchainDescOf(s));
    if (s->swapchain == nullptr)
    {
        s->device.Reset();
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the swapchain for this window could not be made");
    }
    CStrCopyTo(s->adapter, sizeof(s->adapter), s->device->Adapter().id);
    return ENGINE_OK;
}

// Makes the colour target of an off-screen surface the size its window object says (Phase 24 Day 378), or none for a
// size of 0. A target in use by frames still in flight is let go only after the device is idle.
//
// - Returns: false when the device refused the target (the surface skips the frame).
bool EnsureTarget(SurfaceObject* s)
{
    const u32 width = s->window.Width();
    const u32 height = s->window.Height();
    if (s->target.IsValid() && s->target_width == width && s->target_height == height)
    {
        return true;
    }
    if (s->target.IsValid())
    {
        s->device->WaitIdle();  // the frames that drew into it are done
        s->device->DestroyTexture(s->target);
        s->target = rhi::TextureHandle{};
        s->target_width = 0;
        s->target_height = 0;
    }
    if (width == 0 || height == 0)
    {
        return true;
    }
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = rhi::Format::RGBA8_SRGB;
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    desc.debug_name = "offscreen surface";
    s->target = s->device->CreateTexture(desc);
    if (!s->target.IsValid())
    {
        return false;
    }
    s->target_width = width;
    s->target_height = height;
    return true;
}

// Tells the tickets that wait on a surface that they will never be served.
void FailWaiting(SurfaceObject* s, Vector<PixelsObject*>* tickets_by_slot, engine_status why)
{
    for (usize i = 0; i < s->waiting.Size(); ++i)
    {
        PixelsObject* p = s->waiting[i] == kNoTicket ? nullptr : (*tickets_by_slot)[s->waiting[i]];
        if (p != nullptr && p->state == PixelsObject::State::Waiting)
        {
            p->state = PixelsObject::State::Failed;
            p->failure = why;
        }
    }
    s->waiting.Clear();
}

}  // namespace

bool SurfaceKindSupported(u32 kind)
{
#if ENGINE_PLATFORM_MACOS
    return kind == ENGINE_SURFACE_COCOA || kind == ENGINE_SURFACE_OFFSCREEN;  // Metal, which every macOS build has
#elif ENGINE_PLATFORM_WINDOWS
    return kind == ENGINE_SURFACE_WIN32 ||
           kind == ENGINE_SURFACE_OFFSCREEN;  // Direct3D 12 always, Vulkan when the backend is built (Day 354)
#elif ENGINE_RHI_HAVE_VULKAN
    return kind == ENGINE_SURFACE_X11 || kind == ENGINE_SURFACE_WAYLAND || kind == ENGINE_SURFACE_OFFSCREEN;
#else
    (void)kind;
    return false;
#endif
}

SurfaceHost::SurfaceHost() = default;

SurfaceHost::~SurfaceHost()
{
    for (usize i = 0; i < surfaces_.Size(); ++i)
    {
        SurfaceObject* o = surfaces_[i].object.Get();
        if (o != nullptr && o->swapchain != nullptr)
        {
            o->device->DestroySwapchain(o->swapchain);
        }
        if (o != nullptr && o->offscreen && o->device != nullptr)
        {  // the target and the ring leave before their device
            if (!o->device->DeviceLost())
            {
                o->device->WaitIdle();
            }
            DestroyRing(o, true);
            if (o->target.IsValid())
            {
                o->device->DestroyTexture(o->target);
                o->target = rhi::TextureHandle{};
            }
        }
    }
}

engine_status SurfaceHost::Create(const engine_surface_info* given, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = engine_handle{};
    engine_surface_info info = {};
    TRY_SURFACE(CheckInfo(given, &info));
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    void* layer = nullptr;
#if ENGINE_PLATFORM_MACOS
    macos::CocoaLayer cocoa;
    if (info.kind == ENGINE_SURFACE_COCOA)
    {
        const char* why = nullptr;
        if (!macos::AttachLayer(info.display, info.surface, static_cast<f32>(info.scale), &cocoa, &why))
        {
            EngineObjectRelease();
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, why);
        }
        layer = cocoa.layer;
    }
#endif
    UniquePtr<SurfaceObject> object = MakeUnique<SurfaceObject>(NativeOf(info, layer), info.width, info.height,
                                                                static_cast<f32>(info.scale));
#if ENGINE_PLATFORM_MACOS
    object->cocoa = cocoa;        // the object detaches it from here on, also when Build fails below
    cocoa = macos::CocoaLayer();  // the reference moved: the local owns nothing
#endif
    object->present_mode = info.present_mode;
    object->offscreen = info.kind == ENGINE_SURFACE_OFFSCREEN;
    object->backend = BackendOf(info);
    if (info.callbacks != nullptr)
    {
        object->hooks.Set(*info.callbacks);
    }
    {
        LockGuard<Mutex> lock(mutex_);
        const engine_status built = Build(object.Get());
        if (built != ENGINE_OK)
        {
            EngineObjectRelease();
            return built;
        }
        usize slot = 0;
        for (; slot < surfaces_.Size(); ++slot)
        {
            if (surfaces_[slot].object.Get() == nullptr)
            {
                break;
            }
        }
        if (slot == surfaces_.Size())
        {
            if (slot >= kMaxSurfaces)
            {
                if (object->swapchain != nullptr)
                {
                    object->device->DestroySwapchain(object->swapchain);
                }
                EngineObjectRelease();
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many surfaces exist (at most 64)");
            }
            surfaces_.Push(SurfaceSlot());
        }
        surfaces_[slot].generation = NextGeneration();
        surfaces_[slot].object = Move(object);
        *out = MakeObjectHandle(kKindSurface, surfaces_[slot].generation, slot);
    }
    return ENGINE_OK;
}

engine_status SurfaceHost::Destroy(engine_handle surface)
{
    UniquePtr<SurfaceObject> dying;
    {
        LockGuard<Mutex> lock(mutex_);
        SurfaceSlot* slot = nullptr;
        TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
#if ENGINE_PLATFORM_MACOS
        if (slot->object->cocoa.layer != nullptr && !macos::OnMainThread())
        {  // the engine's sublayer leaves the host's view: AppKit's layer tree, the main thread's
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a Cocoa surface is destroyed on the main thread");
        }
#endif
        Vector<PixelsObject*> by_slot;
        for (usize i = 0; i < pixels_.Size(); ++i)
        {
            by_slot.Push(pixels_[i].object.Get());
        }
        FailWaiting(slot->object.Get(), &by_slot, ENGINE_ERROR_CANCELLED);
        dying = Move(slot->object);
    }
    dying->scene.Reset();  // its renderer points at the swapchain
    if (dying->offscreen && dying->device != nullptr)
    {
        dying->device->WaitIdle();
        DestroyRing(dying.Get(), true);
        if (dying->target.IsValid())
        {
            dying->device->DestroyTexture(dying->target);
            dying->target = rhi::TextureHandle{};
        }
    }
    if (dying->swapchain != nullptr)
    {
        dying->device->DestroySwapchain(dying->swapchain);
        dying->swapchain = nullptr;
    }
    dying.Reset();  // the device waits for the GPU and goes
    EngineObjectRelease();
    return ENGINE_OK;
}

namespace
{

static_assert(static_cast<u32>(os::Key::Count) == ENGINE_KEY_COUNT, "engine_key mirrors os::Key");
static_assert(static_cast<u32>(os::CursorShape::Count) == ENGINE_CURSOR_COUNT, "engine_cursor mirrors os::CursorShape");
static_assert(os::kMouseLeft == ENGINE_BUTTON_LEFT && os::kMouseRight == ENGINE_BUTTON_RIGHT &&
                  os::kMouseMiddle == ENGINE_BUTTON_MIDDLE && os::kMouseBack == ENGINE_BUTTON_BACK &&
                  os::kMouseForward == ENGINE_BUTTON_FORWARD,
              "the button bits are the engine's");
static_assert(os::kModShift == ENGINE_MOD_SHIFT && os::kModCtrl == ENGINE_MOD_CTRL && os::kModAlt == ENGINE_MOD_ALT &&
                  os::kModSuper == ENGINE_MOD_SUPER && os::kModCapsLock == ENGINE_MOD_CAPS_LOCK &&
                  os::kModNumLock == ENGINE_MOD_NUM_LOCK,
              "the modifier bits are the engine's");

constexpr usize kMaxTextBytes = 256;
constexpr u32 kAllModifiers = 0x3Fu;

// The length of the UTF-8 scalar that starts at `p` (at most `left` bytes remain), or 0 when it is not well formed
// (a bad lead or continuation byte, an overlong form, a surrogate, above U+10FFFF).
usize ScalarLength(const byte* p, usize left)
{
    const u32 lead = static_cast<u32>(p[0]);
    usize length = 0;
    u32 code = 0;
    if (lead < 0x80)
    {
        return 1;
    }
    else if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
        code = lead & 0x1Fu;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        code = lead & 0x0Fu;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        code = lead & 0x07u;
    }
    else
    {
        return 0;
    }
    if (left < length)
    {
        return 0;
    }
    for (usize i = 1; i < length; ++i)
    {
        const u32 next = static_cast<u32>(p[i]);
        if ((next & 0xC0u) != 0x80u)
        {
            return 0;
        }
        code = (code << 6) | (next & 0x3Fu);
    }
    const u32 minimum = length == 2 ? 0x80u : length == 3 ? 0x800u : 0x10000u;
    if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
    {
        return 0;
    }
    return length;
}

bool Finite(f64 v)
{
    return v == v && v - v == 0.0;
}

}  // namespace

engine_status SurfaceHost::Input(engine_handle surface, const engine_input_event* event)
{
    if (event == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`event` is NULL");
    }
    if (StructSizeOf(event) < sizeof(engine_input_event))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_input_event.struct_size is smaller than the struct");
    }
    const engine_input_event e = *event;
    if ((e.modifiers & ~kAllModifiers) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.modifiers has an unknown bit");
    }
    if (!Finite(e.x) || !Finite(e.y))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.x or y is not a finite number");
    }
    os::InputEvent in;
    in.modifiers = e.modifiers;
    in.time_ns = e.time_ns != 0 ? e.time_ns : os::MonotonicTimeNanos();
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    switch (e.type)
    {
    case ENGINE_INPUT_KEY:
        if (e.key == 0 || e.key >= ENGINE_KEY_COUNT)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.key is not an engine_key");
        }
        in.type = e.down != 0 ? os::InputEventType::KeyDown : os::InputEventType::KeyUp;
        in.key = static_cast<os::Key>(e.key);
        in.repeat = e.down != 0 && e.repeat != 0;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_POINTER_MOVE:
    case ENGINE_INPUT_POINTER_ENTER:
        in.type = e.type == ENGINE_INPUT_POINTER_MOVE ? os::InputEventType::PointerMove
                                                      : os::InputEventType::PointerEnter;
        in.x = e.x;
        in.y = e.y;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_POINTER_LEAVE:
        in.type = os::InputEventType::PointerLeave;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_POINTER_BUTTON:
        if (e.button == 0 || (e.button & (e.button - 1)) != 0 || e.button > ENGINE_BUTTON_FORWARD)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.button is not one ENGINE_BUTTON_* bit");
        }
        in.type = os::InputEventType::PointerButton;
        in.button = e.button;
        in.down = e.down != 0;
        in.x = e.x;
        in.y = e.y;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_SCROLL:
        in.type = os::InputEventType::Wheel;
        in.x = e.x;
        in.y = e.y;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_FOCUS:
        in.type = e.down != 0 ? os::InputEventType::FocusGained : os::InputEventType::FocusLost;
        s->window.PushEvent(in);
        return ENGINE_OK;
    case ENGINE_INPUT_TEXT:
    {
        if (e.text.data == nullptr || e.text.length == 0 || e.text.length > kMaxTextBytes)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.text is empty or longer than 256 bytes");
        }
        const byte* bytes = reinterpret_cast<const byte*>(e.text.data);
        for (usize at = 0; at < e.text.length;)
        {  // checked whole before anything is queued
            const usize length = ScalarLength(bytes + at, e.text.length - at);
            if (length == 0)
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.text is not valid UTF-8");
            }
            at += length;
        }
        for (usize at = 0; at < e.text.length;)
        {
            const usize length = ScalarLength(bytes + at, e.text.length - at);
            in.type = os::InputEventType::Text;
            in.text_length = static_cast<u8>(length);
            MemSet(in.text, 0, sizeof(in.text));
            MemCopy(in.text, bytes + at, length);
            s->window.PushEvent(in);
            at += length;
        }
        return ENGINE_OK;
    }
    case ENGINE_INPUT_TOUCH:
    {
        const u32 id = e.touch_id + 1;  // wraps to 0 only for 0xFFFFFFFF, which is never the active touch below
        if (e.down != 0 && s->active_touch == 0 && id != 0)
        {
            s->active_touch = id;
            in.type = os::InputEventType::PointerEnter;
            in.x = e.x;
            in.y = e.y;
            s->window.PushEvent(in);
            in.type = os::InputEventType::PointerButton;
            in.button = os::kMouseLeft;
            in.down = true;
        }
        else if (id == s->active_touch && id != 0)
        {
            in.x = e.x;
            in.y = e.y;
            if (e.down != 0)
            {
                in.type = os::InputEventType::PointerMove;
            }
            else
            {  // the finger lifted (a lifted touch is `down` 0 at its last position)
                in.type = os::InputEventType::PointerButton;
                in.button = os::kMouseLeft;
                in.down = false;
                s->active_touch = 0;
            }
        }
        else
        {
            return ENGINE_OK;  // another finger: only the first acts as the pointer
        }
        in.x = e.x;
        in.y = e.y;
        s->window.PushEvent(in);
        return ENGINE_OK;
    }
    default: break;
    }
    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_input_event.type is not an engine_input_type");
}

os::ForeignWindow* SurfaceWindowOf(engine_handle surface)
{
    Runtime* runtime = CurrentRuntime();
    SurfaceHost* host = runtime == nullptr ? nullptr : runtime->SurfacesIfCreated();
    return host == nullptr ? nullptr : host->WindowOf(surface);
}

os::ForeignWindow* SurfaceHost::WindowOf(engine_handle surface)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    if (ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot) != ENGINE_OK)
    {
        return nullptr;
    }
    return &slot->object->window;
}

engine_status SurfaceHost::Resized(engine_handle surface, u32 width, u32 height, float scale)
{
    if (width > kMaxDimension || height > kMaxDimension || !(scale >= 0.0f) || !(scale <= 64.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the size or the scale is out of range");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    const f32 applied = scale == 0.0f ? 1.0f : scale;
#if ENGINE_PLATFORM_MACOS
    if (slot->object->cocoa.layer != nullptr)
    {
        if (!macos::OnMainThread())
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a Cocoa surface is resized on the main thread");
        }
        macos::ResizeLayer(slot->object->cocoa, applied);
    }
#endif
    slot->object->window.SetSize(width, height, applied);
    return ENGINE_OK;
}

engine_status SurfaceHost::InjectDeviceLoss(engine_handle surface)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    if (slot->object->device != nullptr)  // none after a recovery that could not make one (Rebind)
    {
        slot->object->device->InjectDeviceLoss();
    }
    return ENGINE_OK;
}

engine_status SurfaceHost::Rebind(engine_handle surface, const engine_surface_info* given)
{
    engine_surface_info info = {};
    TRY_SURFACE(CheckInfo(given, &info));
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    if (NativeOf(info).kind != s->window.GetNativeInfo().kind &&
        s->window.GetNativeInfo().kind != os::NativeWindowKind::Invalid)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a surface keeps the kind of window it was made for");
    }
    if (BackendOf(info) != s->backend)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a surface keeps the graphics API it was made with");
    }
    void* layer = nullptr;
#if ENGINE_PLATFORM_MACOS
    macos::CocoaLayer cocoa;
    if (info.kind == ENGINE_SURFACE_COCOA)
    {  // the new view is checked before anything of the surface changes
        const char* why = nullptr;
        if (!macos::AttachLayer(info.display, info.surface, static_cast<f32>(info.scale), &cocoa, &why))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, why);
        }
        layer = cocoa.layer;
    }
#endif
    s->present_mode = info.present_mode;
    if (info.callbacks != nullptr)
    {
        s->hooks.Set(*info.callbacks);
    }
    // The world to draw after the rebind: the bound one, or the one an earlier rebind that failed unbound (a host
    // retries a rebind until a lost device can be replaced, and cannot bind a world to a lost surface meanwhile: Day
    // 369).
    const engine_handle world = s->scene != nullptr ? s->world : s->kept_world;
    const bool had_scene = world.value != 0;
    s->scene.Reset();  // its renderer points at the swapchain; made again below
    if (had_scene)
    {  // unbound until the new swapchain exists: a failure below leaves the surface without a scene, the world kept
        s->world = engine_handle{};
        s->world_status = ENGINE_ERROR_UNSUPPORTED;
        s->kept_world = world;
    }
    if (s->swapchain != nullptr)  // none while the surface is lost without a device
    {
        s->device->DestroySwapchain(s->swapchain);
        s->swapchain = nullptr;
    }
    if (s->offscreen)
    {  // made again by the next frame, on this device or the new one (the ring holds buffers of the old device)
        if (s->device != nullptr)
        {
            if (!s->device->DeviceLost())
            {
                s->device->WaitIdle();  // a lost device has nothing running; its objects are still ours to destroy
            }
            DestroyRing(s, true);
            if (s->target.IsValid())
            {
                s->device->DestroyTexture(s->target);
            }
        }
        else
        {
            DestroyRing(s, false);
        }
        s->target = rhi::TextureHandle{};
        s->target_width = 0;
        s->target_height = 0;
    }
    s->active_touch = 0;
#if ENGINE_PLATFORM_MACOS
    macos::DetachLayer(&s->cocoa);  // the old view's layer, after the swapchain that used it
    s->cocoa = cocoa;
    cocoa = macos::CocoaLayer();  // the reference moved: the local owns nothing
#endif
    s->window.Rebind(NativeOf(info, layer), info.width, info.height, static_cast<f32>(info.scale));
    if (s->device == nullptr || s->device->DeviceLost())
    {  // a lost device makes no swapchain: the surface gets a new device (Day 369). On Direct3D 12 the adapter's device
       // is new only once every user of the removed one has let it go, so while another surface still holds it this
       // fails (the surface stays lost, with no device), and the host rebinds again after rebinding or destroying the
       // other.
        s->device.Reset();
        const engine_status built = Build(s);
        if (built != ENGINE_OK)
        {
            s->lost = true;
            return built;
        }
    }
    else if (!s->offscreen)
    {
        if (!s->device->RecreateSurface(&s->window))
        {
            s->lost = true;
            return Fail(ENGINE_ERROR_UNSUPPORTED, "the device cannot present to the new window");
        }
        s->swapchain = s->device->CreateSwapchain(SwapchainDescOf(s));
        if (s->swapchain == nullptr)
        {
            s->lost = true;
            return Fail(ENGINE_ERROR_UNSUPPORTED, "the swapchain for the new window could not be made");
        }
    }
    s->lost = false;
    ++s->recoveries;
    if (had_scene)
    {
        Runtime* runtime = CurrentRuntime();
        if (runtime != nullptr && MakeScene(s, runtime->Resources()) == ENGINE_OK)
        {
            s->world = world;
            s->world_status = ENGINE_OK;
        }  // else unbound: the surface clears, with world_status UNSUPPORTED
    }
    s->kept_world = engine_handle{};
    return ENGINE_OK;
}

engine_status SurfaceHost::SetCamera(engine_handle surface, u64 camera_entity)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    s->camera_entity = camera_entity;
    s->camera_status = ENGINE_OK;  // the next frame says whether the entity works
    return ENGINE_OK;
}

engine_status SurfaceHost::PixelRay(engine_handle surface, f32 x, f32 y, f64* origin, f32* direction)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    if (s->scene == nullptr || !s->scene->PixelRay(x, y, origin, direction))
    {
        return Fail(ENGINE_ERROR_NOT_READY, "no frame has drawn a scene on this surface yet");
    }
    return ENGINE_OK;
}

engine_status SurfaceHost::Project(engine_handle surface, const Vec3d& point, f32* out)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    if (s->scene == nullptr || !s->scene->Project(point, out))
    {
        return Fail(ENGINE_ERROR_NOT_READY, "no frame has drawn a scene on this surface yet");
    }
    return ENGINE_OK;
}

/// What the last drawn frame reported (counts and the camera's status), put back to "nothing drawn" when the surface
/// has no scene any more, so `surface_state` does not describe a frame of a world that is gone.
static void ClearSceneReport(SurfaceObject* s)
{
    s->objects_drawn = 0;
    s->entities_unresolved = 0;
    s->lights_refused = 0;
    s->labels_drawn = 0;
    s->labels_refused = 0;
    s->camera_status = ENGINE_OK;
}

engine_status SurfaceHost::SetWorld(engine_handle surface, engine_handle world, ResourceHost* resources)
{
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    if (world.value == 0)
    {
        s->scene.Reset();
        s->world = engine_handle{};
        s->kept_world = engine_handle{};  // the host's choice replaces what a failed rebind kept
        s->world_status = ENGINE_OK;
        ClearSceneReport(s);
        return ENGINE_OK;
    }
    if (s->lost || (s->swapchain == nullptr && !s->offscreen))
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the surface is lost: call surface_rebind first");
    }
    if (s->scene == nullptr)
    {
        TRY_SURFACE(MakeScene(s, resources));
    }
    s->world = world;
    s->kept_world = engine_handle{};
    s->world_status = ENGINE_OK;
    return ENGINE_OK;
}

engine_status SurfaceHost::SetBackground(engine_handle surface, const float* rgba)
{
    if (rgba == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`rgba` is NULL");
    }
    for (int i = 0; i < 4; ++i)
    {
        if (!(rgba[i] >= 0.0f) || !(rgba[i] <= 1.0f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a color component is outside 0 to 1");
        }
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    for (int i = 0; i < 4; ++i)
    {
        slot->object->background[i] = rgba[i];
    }
    return ENGINE_OK;
}

engine_status SurfaceHost::State(engine_handle surface, engine_surface_state* caller)
{
    if (caller == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(caller) < kSurfaceStateFirstSize)
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_surface_state.struct_size is smaller than the struct");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    const SurfaceObject* s = slot->object.Get();
    engine_surface_state full = {};
    engine_surface_state* const out = &full;
    out->struct_size = sizeof(engine_surface_state);
    out->image_width = s->offscreen ? s->target_width : s->swapchain != nullptr ? s->swapchain->Width() : 0;
    out->image_height = s->offscreen ? s->target_height : s->swapchain != nullptr ? s->swapchain->Height() : 0;
    out->visible = s->visible ? 1 : 0;
    out->lost = s->lost ? 1 : 0;
    out->frames_presented = s->presented;
    out->recoveries = s->recoveries;
    out->frames_skipped = s->skipped;
    out->input_dropped = s->window.DroppedEvents();
    out->world_status = s->world_status;
    out->objects_drawn = s->objects_drawn;
    out->entities_unresolved = s->entities_unresolved;
    out->camera_status = s->camera_status;
    out->camera_entity = s->camera_entity;
    out->lights_refused = s->lights_refused;
    out->labels_drawn = s->labels_drawn;
    out->labels_refused = s->labels_refused;
    out->frames_rendered = s->frames_rendered;
    out->frames_acquired = s->frames_acquired;
    out->frames_dropped = s->frames_dropped;
    out->frames_skipped_slow_host = s->frames_skipped_slow;
    out->textures_uploaded = s->scene != nullptr ? s->scene->TextureUploads() : 0;
    out->texture_bytes_uploaded = s->scene != nullptr ? s->scene->TextureBytes() : 0;
    out->present_mode = s->present_mode;
    if (s->swapchain != nullptr)
    {
        const rhi::PresentMode actual = s->swapchain->ActualPresentMode();
        out->present_mode = actual == rhi::PresentMode::Mailbox     ? ENGINE_PRESENT_MAILBOX
                            : actual == rhi::PresentMode::Immediate ? ENGINE_PRESENT_IMMEDIATE
                                                                    : ENGINE_PRESENT_FIFO;
    }
    const u32 written = StructSizeOf(caller) < sizeof(full) ? StructSizeOf(caller) : static_cast<u32>(sizeof(full));
    full.struct_size = written;  // a host built against an older header gets the fields it knows
    MemCopy(caller, &full, written);
    return ENGINE_OK;
}

engine_status SurfaceHost::AcquireFrame(engine_handle surface, engine_frame_image* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_frame_image))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_frame_image.struct_size is smaller than the struct");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    if (!s->offscreen)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "surface_acquire_frame needs an off-screen surface");
    }
    SurfaceObject::HeldFrame* held = nullptr;
    for (SurfaceObject::HeldFrame& h : s->held)
    {
        if (!h.used)
        {
            held = &h;
            break;
        }
    }
    if (held == nullptr)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "two frames are held: release one");
    }
    if (s->device == nullptr || s->lost)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "the surface has no finished frame");
    }
    PollRing(s);
    SurfaceObject::RingSlot* newest = nullptr;
    for (SurfaceObject::RingSlot& r : s->ring)
    {
        if (r.state == SurfaceObject::RingSlot::State::Ready && r.frame <= s->last_acquired_frame)
        {  // older than a frame the host already took: never handed out after it
            r.state = SurfaceObject::RingSlot::State::Free;
            ++s->frames_dropped;
            continue;
        }
        if (r.state == SurfaceObject::RingSlot::State::Ready && (newest == nullptr || r.frame > newest->frame))
        {
            newest = &r;
        }
    }
    if (newest == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "no frame has finished since the last acquire");
    }
    const u32 row = newest->width * 4;
    held->rgba.Resize(static_cast<usize>(row) * newest->height);
    const byte* mapped = static_cast<const byte*>(s->device->MapBuffer(newest->buffer));
    if (mapped == nullptr)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the read-back buffer could not be mapped");
    }
    for (u32 y = 0; y < newest->height; ++y)
    {
        MemCopy(held->rgba.Data() + static_cast<usize>(y) * row, mapped + static_cast<usize>(y) * newest->pitch, row);
    }
    s->device->UnmapBuffer(newest->buffer);
    held->used = true;
    held->width = newest->width;
    held->height = newest->height;
    held->frame = newest->frame;
    // every finished frame older than this one is never taken
    u64 older = 0;
    for (SurfaceObject::RingSlot& r : s->ring)
    {
        if (r.state == SurfaceObject::RingSlot::State::Ready)
        {
            if (&r != newest && r.frame > s->last_acquired_frame)
            {
                ++older;
            }
            r.state = SurfaceObject::RingSlot::State::Free;
        }
    }
    // frames that were replaced in the ring were counted when replaced; ones that finished and are passed over now:
    s->frames_dropped += older;
    out->width = held->width;
    out->height = held->height;
    out->stride = row;
    out->data = held->rgba.Data();
    out->frame = held->frame;
    out->dropped = older;
    s->last_acquired_frame = held->frame;
    ++s->frames_acquired;
    return ENGINE_OK;
}

engine_status SurfaceHost::ReleaseFrame(engine_handle surface, const engine_frame_image* image)
{
    if (image == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`image` is NULL");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    SurfaceObject* s = slot->object.Get();
    for (SurfaceObject::HeldFrame& h : s->held)
    {
        if (h.used && h.rgba.Data() == image->data)
        {
            h.used = false;
            h.rgba.Reset();
            return ENGINE_OK;
        }
    }
    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the frame is not held from this surface");
}

engine_status SurfaceHost::ReadPixels(engine_handle surface, engine_handle* ticket)
{
    if (ticket == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`ticket` is NULL");
    }
    *ticket = engine_handle{};
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    usize index = 0;
    for (; index < pixels_.Size(); ++index)
    {
        if (pixels_[index].object.Get() == nullptr)
        {
            break;
        }
    }
    if (index == pixels_.Size())
    {
        if (index >= kMaxPixelTickets)
        {
            EngineObjectRelease();
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many read-back tickets exist (at most 256)");
        }
        pixels_.Push(PixelsSlot());
    }
    pixels_[index].object = MakeUnique<PixelsObject>();
    pixels_[index].generation = NextGeneration();
    pixels_[index].object->surface_generation = slot->generation;
    slot->object->waiting.Push(index);
    *ticket = MakeObjectHandle(kKindPixels, pixels_[index].generation, index);
    return ENGINE_OK;
}

engine_status SurfaceHost::PixelsFinish(engine_handle ticket, engine_pixels* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_pixels))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_pixels.struct_size is smaller than the struct");
    }
    *out = engine_pixels{};
    out->struct_size = sizeof(engine_pixels);
    LockGuard<Mutex> lock(mutex_);
    PixelsSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(pixels_, ticket, kKindPixels, "read-back ticket", &slot));
    PixelsObject* p = slot->object.Get();
    switch (p->state)
    {
    case PixelsObject::State::Waiting:
        return Fail(ENGINE_ERROR_NOT_READY, "no frame has been rendered since the request");
    case PixelsObject::State::Failed: return Fail(p->failure, "the read-back failed");
    case PixelsObject::State::Taken:
        return Fail(ENGINE_ERROR_NOT_FOUND, "the pixels of this ticket were already taken");
    case PixelsObject::State::Done: break;
    }
    out->width = p->width;
    out->height = p->height;
    out->stride = p->width * 4;
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(p->rgba.Data()), p->rgba.Size()), &out->data);
    p->rgba = Vector<byte>();
    p->state = PixelsObject::State::Taken;
    return ENGINE_OK;
}

engine_status SurfaceHost::PixelsDestroy(engine_handle ticket)
{
    {
        LockGuard<Mutex> lock(mutex_);
        PixelsSlot* slot = nullptr;
        TRY_SURFACE(ResolveSlot(pixels_, ticket, kKindPixels, "read-back ticket", &slot));
        const usize index = static_cast<usize>(slot - pixels_.Data());
        for (usize i = 0; i < surfaces_.Size(); ++i)
        {
            SurfaceObject* s = surfaces_[i].object.Get();
            if (s == nullptr)
            {
                continue;
            }
            for (usize w = 0; w < s->waiting.Size(); ++w)
            {
                if (s->waiting[w] == index)
                {
                    s->waiting[w] = kNoTicket;
                }
            }
        }
        slot->object.Reset();
    }
    EngineObjectRelease();
    return ENGINE_OK;
}

// ---- rendering

namespace
{

// The pixels of the image just drawn, as RGBA8 (the swapchain's own encoding: sRGB when the format is).
bool ReadImage(SurfaceObject* s, rhi::TextureHandle image, PixelsObject* out)
{
    const rhi::Format format = s->offscreen ? rhi::Format::RGBA8_SRGB : s->swapchain->ImageFormat();
    const bool bgra = format == rhi::Format::BGRA8_UNorm || format == rhi::Format::BGRA8_SRGB;
    const bool rgba = format == rhi::Format::RGBA8_UNorm || format == rhi::Format::RGBA8_SRGB;
    if (!bgra && !rgba)
    {
        return false;
    }
    const u32 width = s->offscreen ? s->target_width : s->swapchain->Width();
    const u32 height = s->offscreen ? s->target_height : s->swapchain->Height();
    out->rgba.Resize(static_cast<usize>(width) * height * 4);
    if (!s->device->ReadbackTexture(image, 0, 0, out->rgba.Data(), out->rgba.Size(),
                                    s->offscreen ? rhi::ResourceState::CopySrc : rhi::ResourceState::Present))
    {
        return false;
    }
    if (bgra)
    {
        for (usize i = 0; i + 3 < out->rgba.Size(); i += 4)
        {
            const byte blue = out->rgba[i];
            out->rgba[i] = out->rgba[i + 2];
            out->rgba[i + 2] = blue;
        }
    }
    out->width = width;
    out->height = height;
    return true;
}

// Drops the ring's GPU buffers (the device must be idle with them, or about to be destroyed).
void DestroyRing(SurfaceObject* s, bool destroy_buffers)
{
    for (SurfaceObject::RingSlot& slot : s->ring)
    {
        if (destroy_buffers && slot.buffer.IsValid() && s->device != nullptr)
        {
            s->device->DestroyBuffer(slot.buffer);
        }
        slot = SurfaceObject::RingSlot{};
    }
}

// Moves the pending slots whose copy the GPU has finished to Ready, without waiting.
void PollRing(SurfaceObject* s)
{
    const rhi::FenceValue done = s->device->CompletedFence();
    for (SurfaceObject::RingSlot& slot : s->ring)
    {
        if (slot.state == SurfaceObject::RingSlot::State::Pending && done >= slot.fence)
        {
            slot.state = SurfaceObject::RingSlot::State::Ready;
        }
    }
}

// Starts the copy of the frame just drawn into a free ring slot; with no free slot the frame is skipped and counted.
void QueueFrame(SurfaceObject* s)
{
    ++s->frames_rendered;
    PollRing(s);
    SurfaceObject::RingSlot* pick = nullptr;
    for (SurfaceObject::RingSlot& slot : s->ring)
    {
        if (slot.state == SurfaceObject::RingSlot::State::Free)
        {
            pick = &slot;
            break;
        }
    }
    if (pick == nullptr)
    {  // replace the oldest finished frame the host has not taken (it counts as dropped when a newer one is taken)
        for (SurfaceObject::RingSlot& slot : s->ring)
        {
            if (slot.state == SurfaceObject::RingSlot::State::Ready && (pick == nullptr || slot.frame < pick->frame))
            {
                pick = &slot;
            }
        }
        if (pick != nullptr)
        {
            ++s->frames_dropped;
        }
    }
    if (pick == nullptr)
    {
        ++s->frames_skipped_slow;
        return;
    }
    const u32 width = s->target_width;
    const u32 height = s->target_height;
    const u32 pitch = (width * 4 + 255u) & ~255u;
    const u64 bytes = static_cast<u64>(pitch) * height;
    if (!pick->buffer.IsValid() || pick->buffer_bytes != bytes)
    {
        if (pick->buffer.IsValid())
        {
            s->device->DestroyBuffer(pick->buffer);
            pick->buffer = rhi::BufferHandle{};
        }
        rhi::BufferDesc desc;
        desc.size = bytes;
        desc.usage = rhi::BufferUsage::CopyDst;
        desc.location = rhi::MemoryLocation::GpuToCpu;
        desc.debug_name = "offscreen read-back";
        pick->buffer = s->device->CreateBuffer(desc);
        pick->buffer_bytes = bytes;
        if (!pick->buffer.IsValid())
        {
            *pick = SurfaceObject::RingSlot{};
            ++s->frames_skipped_slow;
            return;
        }
    }
    s->device->BeginFrame();
    rhi::CommandList* list = s->device->CreateCommandList();
    rhi::FenceValue fence = 0;
    if (list != nullptr)
    {
        list->Begin();
        rhi::BufferTextureCopy region;
        region.buffer_row_pitch = pitch;
        list->CopyTextureToBuffer(s->target, pick->buffer, region);
        list->End();
        rhi::CommandList* lists[] = {list};
        fence = s->device->Submit(lists, 1);
    }
    s->device->EndFrame();
    if (fence == 0)
    {
        pick->state = SurfaceObject::RingSlot::State::Free;
        ++s->frames_skipped_slow;
        return;
    }
    pick->fence = fence;
    pick->frame = s->frames_rendered;
    pick->width = width;
    pick->height = height;
    pick->pitch = pitch;
    pick->state = SurfaceObject::RingSlot::State::Pending;
}

// Gives the tickets waiting on a surface the pixels of the image just drawn (read once, shared by the tickets).
void ServeReadbacks(SurfaceObject* s, rhi::TextureHandle image, Vector<PixelsObject*>& by_slot)
{
    if (s->waiting.Empty())
    {
        return;
    }
    PixelsObject first;
    const bool ok = ReadImage(s, image, &first);
    for (usize w = 0; w < s->waiting.Size(); ++w)
    {
        PixelsObject* p = s->waiting[w] == kNoTicket ? nullptr : by_slot[s->waiting[w]];
        if (p == nullptr || p->state != PixelsObject::State::Waiting)
        {
            continue;
        }
        if (ok)
        {
            p->width = first.width;
            p->height = first.height;
            p->rgba.Resize(first.rgba.Size());
            MemCopy(p->rgba.Data(), first.rgba.Data(), first.rgba.Size());
            p->state = PixelsObject::State::Done;
        }
        else
        {
            p->state = PixelsObject::State::Failed;
            p->failure = ENGINE_ERROR_UNSUPPORTED;
        }
    }
    s->waiting.Clear();
}

}  // namespace

namespace
{

void BeforePresent(rhi::TextureHandle image, void* user)
{
    SurfaceObject* s = static_cast<SurfaceObject*>(user);
    if (s->frame_by_slot != nullptr)
    {
        ServeReadbacks(s, image, *s->frame_by_slot);
    }
}

}  // namespace

void SurfaceHost::RenderAll(Runtime& runtime, f32 delta_seconds)
{
    LockGuard<Mutex> lock(mutex_);
    Vector<PixelsObject*> by_slot;
    for (usize i = 0; i < pixels_.Size(); ++i)
    {
        by_slot.Push(pixels_[i].object.Get());
    }
    for (usize k = 0; k < surfaces_.Size(); ++k)
    {
        SurfaceObject* s = surfaces_[k].object.Get();
        if (s == nullptr)
        {
            continue;
        }
        s->visible = false;
        if (s->lost || (s->swapchain == nullptr && !s->offscreen))
        {
            ++s->skipped;
            FailWaiting(s, &by_slot,
                        ENGINE_ERROR_UNSUPPORTED);  // a read-back asked after the loss must not wait for a rebind
            continue;
        }
        if (s->window.IsMinimized())
        {
            ++s->skipped;
            continue;
        }
#if ENGINE_PLATFORM_MACOS
        if (s->cocoa.layer != nullptr && !macos::OnMainThread())
        {  // AppKit's thread only (engine_c.h, ENGINE_SURFACE_COCOA): a frame on another thread leaves the surface
           // alone, and the read-backs waiting on it fail rather than wait for a main-thread frame that may never come
            ++s->skipped;
            FailWaiting(s, &by_slot, ENGINE_ERROR_INVALID_ARGUMENT);
            continue;
        }
#endif
        if (s->offscreen && !EnsureTarget(s))
        {
            ++s->skipped;
            FailWaiting(s, &by_slot, ENGINE_ERROR_UNSUPPORTED);
            continue;
        }
        renderer::FrameTarget frame_target;
        if (s->offscreen)
        {
            frame_target.texture = s->target;
            frame_target.width = s->target_width;
            frame_target.height = s->target_height;
            frame_target.format = rhi::Format::RGBA8_SRGB;
            frame_target.initial = rhi::ResourceState::Undefined;
            frame_target.final_state = rhi::ResourceState::CopySrc;
        }
        if (s->scene != nullptr)
        {
            WorldObject* world = FindWorld(runtime, s->world);
            if (world == nullptr)
            {  // destroyed under the surface: back to the background color, with the reason
                s->scene.Reset();
                s->world = engine_handle{};
                s->world_status = ENGINE_ERROR_STALE_HANDLE;
                ClearSceneReport(s);
            }
            else
            {
                s->frame_by_slot = &by_slot;
                const u32 width = s->offscreen ? s->target_width : s->swapchain->Width();
                const u32 height = s->offscreen ? s->target_height : s->swapchain->Height();
                const SceneFrameReport report = s->scene->Render(*world, s->camera_entity, width, height, 0.0f,
                                                                 delta_seconds, s->offscreen ? &frame_target : nullptr);
                s->camera_status = static_cast<engine_status>(report.camera_status);
                s->lights_refused = report.lights_refused;
                s->labels_drawn = report.labels_drawn;
                s->labels_refused = report.labels_refused;
                s->frame_by_slot = nullptr;
                s->objects_drawn = report.objects_drawn;
                s->entities_unresolved = report.entities_unresolved;
                if (report.status == renderer::FrameStatus::Ok)
                {
                    s->visible = true;
                    ++s->presented;
                    if (s->offscreen)
                    {  // there is no present: the tickets are served from the target now
                        ServeReadbacks(s, s->target, by_slot);
                        QueueFrame(s);
                    }
                    // a swapchain surface's ticket asked for this frame was served before the present
                }
                else
                {
                    ++s->skipped;
                    if (report.status == renderer::FrameStatus::DeviceLost)
                    {
                        s->lost = true;
                        FailWaiting(s, &by_slot, ENGINE_ERROR_UNSUPPORTED);
                    }
                }
                continue;
            }
        }
        if (s->offscreen)
        {
            if (!s->target.IsValid())
            {  // a size of 0: nothing is drawn
                ++s->skipped;
                continue;
            }
            s->device->BeginFrame();
            rhi::CommandList* list = s->device->CreateCommandList();
            list->Begin();
            list->Barrier(s->target, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
            rhi::RenderingDesc pass;
            pass.color_count = 1;
            pass.color[0].texture = s->target;
            pass.color[0].load = rhi::LoadOp::Clear;
            for (int c = 0; c < 4; ++c)
            {
                pass.color[0].clear_color[c] = s->background[c];
            }
            list->BeginRendering(pass);
            list->EndRendering();
            list->Barrier(s->target, rhi::ResourceState::ColorAttachment, rhi::ResourceState::CopySrc);
            list->End();
            rhi::CommandList* lists[] = {list};
            s->device->Submit(lists, 1);
            s->device->EndFrame();
            if (s->device->DeviceLost())
            {
                s->lost = true;
                ++s->skipped;
                FailWaiting(s, &by_slot, ENGINE_ERROR_UNSUPPORTED);
                continue;
            }
            ServeReadbacks(s, s->target, by_slot);
            QueueFrame(s);
            s->visible = true;
            ++s->presented;
            continue;
        }
        s->device->BeginFrame();
        const rhi::SwapchainStatus acquired = s->swapchain->Acquire();
        if (acquired == rhi::SwapchainStatus::Minimized)
        {
            s->device->EndFrame();
            ++s->skipped;
            continue;
        }
        if (acquired == rhi::SwapchainStatus::Lost)
        {  // the host's window went away under us: the device keeps, the host calls surface_rebind
            s->device->EndFrame();
            s->lost = true;
            ++s->skipped;
            FailWaiting(s, &by_slot, ENGINE_ERROR_UNSUPPORTED);
            continue;
        }
        const rhi::TextureHandle image = s->swapchain->CurrentTexture();
        rhi::CommandList* list = s->device->CreateCommandList();
        list->Begin();
        list->Barrier(image, rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment);
        rhi::RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = image;
        pass.color[0].load = rhi::LoadOp::Clear;
        for (int c = 0; c < 4; ++c)
        {
            pass.color[0].clear_color[c] = s->background[c];
        }
        list->BeginRendering(pass);
        list->EndRendering();
        list->Barrier(image, rhi::ResourceState::ColorAttachment, rhi::ResourceState::Present);
        list->End();
        rhi::CommandList* lists[] = {list};
        s->device->Submit(lists, 1);
        s->device->EndFrame();
        ServeReadbacks(s, image, by_slot);  // before Present: a Metal image is taken back by it
        const rhi::SwapchainStatus presented = s->swapchain->Present();
        if (presented == rhi::SwapchainStatus::Lost)
        {
            s->lost = true;
            continue;
        }
        s->visible = true;
        ++s->presented;
    }
}

// ---- the functions of the table

namespace
{

engine_status HostOfRuntime(Runtime* runtime, SurfaceHost** out)
{
    *out = runtime->Surfaces();
    return ENGINE_OK;
}

engine_status HostOfObject(engine_handle handle, SurfaceHost** out)
{
    *out = nullptr;
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(handle.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    *out = runtime->SurfacesIfCreated();
    if (*out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the handle was never issued by this engine");
    }
    return ENGINE_OK;
}

}  // namespace

engine_status SurfaceCreateFn(engine_handle engine, const engine_surface_info* info, engine_handle* out)
{
    Runtime* runtime = nullptr;
    TRY_SURFACE(ResolveRuntime(engine, &runtime));
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfRuntime(runtime, &host));
    return host->Create(info, out);
}

engine_status SurfaceDestroyFn(engine_handle surface)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->Destroy(surface);
}

engine_status SurfaceInputFn(engine_handle surface, const engine_input_event* event)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->Input(surface, event);
}

engine_status SurfaceResizedFn(engine_handle surface, u32 width, u32 height, float scale)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->Resized(surface, width, height, scale);
}

engine_status SurfaceRebindFn(engine_handle surface, const engine_surface_info* info)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->Rebind(surface, info);
}

engine_status SurfaceSetBackgroundFn(engine_handle surface, const float* rgba)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->SetBackground(surface, rgba);
}

engine_status SurfaceSetWorldFn(engine_handle surface, engine_handle world)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    Runtime* runtime = CurrentRuntime();
    if (world.value != 0)
    {  // the world is checked under its own mutex, and before the surface's: the frame takes them in that order
        LockGuard<Mutex> lock(runtime->world_mutex);
        engine_status found = ENGINE_OK;
        if (FindWorld(*runtime, world, &found) == nullptr)
        {
            return found;
        }
    }
    return host->SetWorld(surface, world, runtime->Resources());
}

engine_status SurfaceSetCameraFn(engine_handle surface, u64 camera_entity)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->SetCamera(surface, camera_entity);
}

engine_status SurfacePixelRayFn(engine_handle surface, float x, float y, double* origin, float* direction)
{
    if (origin == nullptr || direction == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`origin` or `direction` is NULL");
    }
    if (!(x - x == 0.0f) || !(y - y == 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the pixel is not finite");
    }
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->PixelRay(surface, x, y, origin, direction);
}

engine_status SurfaceAcquireFrameFn(engine_handle surface, engine_frame_image* out)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->AcquireFrame(surface, out);
}

engine_status SurfaceReleaseFrameFn(engine_handle surface, const engine_frame_image* image)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->ReleaseFrame(surface, image);
}

engine_status SurfaceProjectFn(engine_handle surface, const double* point, float* out)
{
    if (point == nullptr || out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`point` or `out` is NULL");
    }
    for (int i = 0; i < 3; ++i)
    {
        if (!(point[i] - point[i] == 0.0) || point[i] > 1.0e12 || point[i] < -1.0e12)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the point is not finite or is beyond 1e12 metres");
        }
    }
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->Project(surface, Vec3d(point[0], point[1], point[2]), out);
}

engine_status SurfaceStateFn(engine_handle surface, engine_surface_state* out)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->State(surface, out);
}

engine_status SurfaceReadPixelsFn(engine_handle surface, engine_handle* ticket)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(surface, &host));
    return host->ReadPixels(surface, ticket);
}

engine_status PixelsFinishFn(engine_handle ticket, engine_pixels* out)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(ticket, &host));
    return host->PixelsFinish(ticket, out);
}

engine_status PixelsDestroyFn(engine_handle ticket)
{
    SurfaceHost* host = nullptr;
    TRY_SURFACE(HostOfObject(ticket, &host));
    return host->PixelsDestroy(ticket);
}

}  // namespace vse::engine_c
