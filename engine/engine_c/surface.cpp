#include "engine_c/surface.h"

#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/handles.h"
#include "engine_c/runtime.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "platform/foreign_window.h"
#include "rhi/backends/backends.h"
#include "rhi/command_list.h"
#include "rhi/device.h"
#include "rhi/rhi.h"
#include "rhi/swapchain.h"

#define TRY_SURFACE(expression)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status try_surface_status = (expression);                                                         \
        if (try_surface_status != ENGINE_OK)                                                                           \
        {                                                                                                              \
            return try_surface_status;                                                                                 \
        }                                                                                                              \
    } while (0)

namespace engine_placeholder::engine_c
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

struct SurfaceObject
{
    SurfaceObject(const os::NativeWindowInfo& info, u32 width, u32 height, f32 scale)
        : window(info, width, height, scale)
    {
    }

    os::ForeignWindow window;
    rhi::DevicePtr device;
    rhi::Swapchain* swapchain = nullptr;
    f32 background[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    u64 presented = 0;
    u64 recoveries = 0;
    u64 skipped = 0;
    bool visible = false;
    bool lost = false;
    Vector<usize> waiting;  // slots of the read-back tickets waiting for a frame
};

namespace
{

os::NativeWindowInfo NativeOf(const engine_surface_info& info)
{
    os::NativeWindowInfo native;
    if (info.kind == ENGINE_SURFACE_WAYLAND)
    {
        native.kind = os::NativeWindowKind::Wayland;
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
    if (out->flags != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_surface_info.flags has an unknown bit");
    }
    if (!SurfaceKindSupported(out->kind))
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "this build cannot render into a window of that kind");
    }
    if (out->display == nullptr ||
        (out->kind == ENGINE_SURFACE_WAYLAND ? out->surface == nullptr : out->window_id == 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the native handles of the window are missing");
    }
    if (out->width > kMaxDimension || out->height > kMaxDimension || !(out->scale >= 0.0f) || !(out->scale <= 64.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the size or the scale is out of range");
    }
    if (out->scale == 0.0f)
    {
        out->scale = 1.0f;
    }
    return ENGINE_OK;
}

// Makes the device and the swapchain of a surface from its window.
engine_status Build(SurfaceObject* s)
{
    rhi::RegisterCompiledBackends();
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Vulkan;
    info.application_name = "engine";
    info.window = &s->window;
    info.pipeline_cache = false;
    s->device = rhi::CreateDevice(info);
    if (s->device.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "no Vulkan device can present to this window");
    }
    s->swapchain = s->device->CreateSwapchain(rhi::SwapchainDesc());
    if (s->swapchain == nullptr)
    {
        s->device.Reset();
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the swapchain for this window could not be made");
    }
    return ENGINE_OK;
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
#if ENGINE_RHI_HAVE_VULKAN
    return kind == ENGINE_SURFACE_X11 || kind == ENGINE_SURFACE_WAYLAND;
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
        if (surfaces_[i].object.Get() != nullptr && surfaces_[i].object->swapchain != nullptr)
        {
            surfaces_[i].object->device->DestroySwapchain(surfaces_[i].object->swapchain);
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
    UniquePtr<SurfaceObject> object = MakeUnique<SurfaceObject>(NativeOf(info), info.width, info.height,
                                                                static_cast<f32>(info.scale));
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
                object->device->DestroySwapchain(object->swapchain);
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
        Vector<PixelsObject*> by_slot;
        for (usize i = 0; i < pixels_.Size(); ++i)
        {
            by_slot.Push(pixels_[i].object.Get());
        }
        FailWaiting(slot->object.Get(), &by_slot, ENGINE_ERROR_CANCELLED);
        dying = Move(slot->object);
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

engine_status SurfaceHost::Resized(engine_handle surface, u32 width, u32 height, float scale)
{
    if (width > kMaxDimension || height > kMaxDimension || !(scale >= 0.0f) || !(scale <= 64.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the size or the scale is out of range");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    slot->object->window.SetSize(width, height, scale == 0.0f ? 1.0f : scale);
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
    s->device->DestroySwapchain(s->swapchain);
    s->swapchain = nullptr;
    s->window.Rebind(NativeOf(info), info.width, info.height, static_cast<f32>(info.scale));
    if (!s->device->RecreateSurface(&s->window))
    {
        s->lost = true;
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the device cannot present to the new window");
    }
    s->swapchain = s->device->CreateSwapchain(rhi::SwapchainDesc());
    if (s->swapchain == nullptr)
    {
        s->lost = true;
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the swapchain for the new window could not be made");
    }
    s->lost = false;
    ++s->recoveries;
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

engine_status SurfaceHost::State(engine_handle surface, engine_surface_state* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (StructSizeOf(out) < sizeof(engine_surface_state))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_surface_state.struct_size is smaller than the struct");
    }
    LockGuard<Mutex> lock(mutex_);
    SurfaceSlot* slot = nullptr;
    TRY_SURFACE(ResolveSlot(surfaces_, surface, kKindSurface, "surface", &slot));
    const SurfaceObject* s = slot->object.Get();
    *out = engine_surface_state{};
    out->struct_size = sizeof(engine_surface_state);
    out->image_width = s->swapchain != nullptr ? s->swapchain->Width() : 0;
    out->image_height = s->swapchain != nullptr ? s->swapchain->Height() : 0;
    out->visible = s->visible ? 1 : 0;
    out->lost = s->lost ? 1 : 0;
    out->frames_presented = s->presented;
    out->recoveries = s->recoveries;
    out->frames_skipped = s->skipped;
    return ENGINE_OK;
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
    const rhi::Format format = s->swapchain->ImageFormat();
    const bool bgra = format == rhi::Format::BGRA8_UNorm || format == rhi::Format::BGRA8_SRGB;
    const bool rgba = format == rhi::Format::RGBA8_UNorm || format == rhi::Format::RGBA8_SRGB;
    if (!bgra && !rgba)
    {
        return false;
    }
    const u32 width = s->swapchain->Width();
    const u32 height = s->swapchain->Height();
    out->rgba.Resize(static_cast<usize>(width) * height * 4);
    if (!s->device->ReadbackTexture(image, 0, 0, out->rgba.Data(), out->rgba.Size(), rhi::ResourceState::Present))
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

void SurfaceHost::RenderAll()
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
        if (s->lost || s->swapchain == nullptr)
        {
            ++s->skipped;
            continue;
        }
        if (s->window.IsMinimized())
        {
            ++s->skipped;
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

}  // namespace engine_placeholder::engine_c
