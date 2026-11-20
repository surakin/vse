#pragma once

// The Metal layer of a Cocoa surface of the C API (Phase 23 Day 355): the host gives its NSView, and either a
// CAMetalLayer it made or none. Without one, the view's own layer is used when it is a CAMetalLayer; otherwise the
// engine adds a CAMetalLayer of its own as a sublayer that fills the view and removes it again. No Apple type is named
// here, so surface.cpp stays C++; the implementation is cocoa_surface.mm (ARC, every entry inside an autorelease pool).
#include "types.h"

namespace vse::engine_c::macos
{

// The layer a surface draws into. `layer` is retained (+1) while it is bound; `view` is borrowed from the host. Plain
// data that owns a reference: whoever takes it over clears the source (see Release in surface.cpp's use), and exactly
// one DetachLayer gives the reference back.
struct CocoaLayer
{
    void* view = nullptr;            // the host's NSView*
    void* layer = nullptr;           // the CAMetalLayer*, retained
    bool owned = false;              // the engine added it as a sublayer of the view's layer
    bool made_layer_backed = false;  // the engine set the view's wantsLayer (restored when the layer goes)
};

// Whether the calling thread is the main thread (AppKit's thread: a Cocoa surface is made, resized and drawn there).
bool OnMainThread();

// Finds or makes the Metal layer for the host's `view` (an NSView*) and optional `layer` (a CAMetalLayer*); a layer the
// engine adds fills the view at `scale` pixels per point (the drawable size is the swapchain's, from the size the host
// reports). Returns false with `*why` set (a static sentence) when the handles are not what they must be.
bool AttachLayer(void* view, void* layer, f32 scale, CocoaLayer* out, const char** why);

// Follows the host's new size: an owned sublayer takes the view's bounds and the scale (a host's own layer is the
// host's to size).
void ResizeLayer(const CocoaLayer& bound, f32 scale);

// Lets the layer go: an owned sublayer is removed from the view's layer; the reference is released. Safe on an empty
// CocoaLayer.
void DetachLayer(CocoaLayer* bound);

}  // namespace vse::engine_c::macos
