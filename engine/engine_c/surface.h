#pragma once
//! The surfaces of one engine (Phase 23 Day 352): windows the host owns, that the engine renders into on the host's
//! connection, and the pixel read-backs of their frames. Internal to the module `engine_c`: nothing here reaches the
//! host but through `engine_c.h`.
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::os
{
class ForeignWindow;
}

namespace engine_placeholder::engine_c
{

struct SurfaceObject;
struct PixelsObject;

/// The surfaces and read-back tickets of an engine.
///
/// - Thread-safety: thread-safe; one mutex guards everything, so a call waits for a frame that is rendering. The host's
///   frame thread makes the calls (the API's rule for `frame`).
/// - Ownership: owns the surfaces (device, swapchain, window description) and the tickets; each counts as an object of
///   the engine until it is destroyed.
class SurfaceHost
{
public:
    SurfaceHost();
    /// Destroys what is left (the engine refuses to be destroyed while a surface or ticket is out).
    ~SurfaceHost();
    SurfaceHost(const SurfaceHost&) = delete;
    SurfaceHost& operator=(const SurfaceHost&) = delete;

    engine_status Create(const engine_surface_info* info, engine_handle* out);
    engine_status Destroy(engine_handle surface);
    engine_status Input(engine_handle surface, const engine_input_event* event);
    /// The window of a surface (see `SurfaceWindowOf`), or null.
    os::ForeignWindow* WindowOf(engine_handle surface);
    engine_status Resized(engine_handle surface, u32 width, u32 height, float scale);
    engine_status Rebind(engine_handle surface, const engine_surface_info* info);
    engine_status SetBackground(engine_handle surface, const float* rgba);
    engine_status State(engine_handle surface, engine_surface_state* out);
    engine_status ReadPixels(engine_handle surface, engine_handle* ticket);
    engine_status PixelsFinish(engine_handle ticket, engine_pixels* out);
    engine_status PixelsDestroy(engine_handle ticket);

    /// Renders one frame into every surface (called by `frame`).
    ///
    /// - Thread-safety: thread-safe.
    void RenderAll();

private:
    struct SurfaceSlot
    {
        UniquePtr<SurfaceObject> object;
        u32 generation = 0;
    };
    struct PixelsSlot
    {
        UniquePtr<PixelsObject> object;
        u32 generation = 0;
    };
    Mutex mutex_;
    Vector<SurfaceSlot> surfaces_;
    Vector<PixelsSlot> pixels_;
};

/// Whether this build can render into a host window of `kind` (an `ENGINE_SURFACE_*` value).
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
bool SurfaceKindSupported(u32 kind);

/// The window behind a surface, for the tests that bind the engine's UI to it (the host's input arrives in its event
/// queue, the UI's cursor and clipboard requests leave through its hooks). Null for a handle that is not a live
/// surface.
///
/// - Thread-safety: the window is thread-compatible: the thread that forwards the input uses it, and the surface must
/// outlive the pointer.
/// - Fails: returns null.
os::ForeignWindow* SurfaceWindowOf(engine_handle surface);

// The functions of the table that serve the surfaces (engine_c.cpp lists them in the table).
engine_status SurfaceCreateFn(engine_handle engine, const engine_surface_info* info, engine_handle* out);
engine_status SurfaceDestroyFn(engine_handle surface);
engine_status SurfaceInputFn(engine_handle surface, const engine_input_event* event);
engine_status SurfaceResizedFn(engine_handle surface, u32 width, u32 height, float scale);
engine_status SurfaceRebindFn(engine_handle surface, const engine_surface_info* info);
engine_status SurfaceSetBackgroundFn(engine_handle surface, const float* rgba);
engine_status SurfaceStateFn(engine_handle surface, engine_surface_state* out);
engine_status SurfaceReadPixelsFn(engine_handle surface, engine_handle* ticket);
engine_status PixelsFinishFn(engine_handle ticket, engine_pixels* out);
engine_status PixelsDestroyFn(engine_handle ticket);

}  // namespace engine_placeholder::engine_c
