// The Metal layer of a Cocoa surface of the C API (Phase 23 Day 355; cocoa_surface.h).

#include "engine_c/macos/cocoa_surface.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

namespace engine_placeholder::engine_c::macos
{

namespace
{

// The sublayer's frame and scale: the view's bounds, in points, with `scale` pixels per point.
void FitSublayer(NSView* view, CAMetalLayer* layer, f32 scale)
{
    [CATransaction begin];
    [CATransaction setDisableActions:YES];  // no implicit animation of the frame
    layer.frame = view.bounds;
    layer.contentsScale = scale > 0.0f ? scale : 1.0f;
    [CATransaction commit];
}

}  // namespace

bool OnMainThread()
{
    @autoreleasepool
    {
        return NSThread.isMainThread;
    }
}

bool AttachLayer(void* view_handle, void* layer_handle, f32 scale, CocoaLayer* out, const char** why)
{
    @autoreleasepool
    {
        *out = CocoaLayer();
        id view_object = (__bridge id)view_handle;
        if (![view_object isKindOfClass:[NSView class]])
        {
            *why = "engine_surface_info.display is not an NSView";
            return false;
        }
        NSView* view = (NSView*)view_object;
        CAMetalLayer* layer = nil;
        bool owned = false;
        if (layer_handle != nullptr)
        {
            id layer_object = (__bridge id)layer_handle;
            if (![layer_object isKindOfClass:[CAMetalLayer class]])
            {
                *why = "engine_surface_info.surface is not a CAMetalLayer";
                return false;
            }
            layer = (CAMetalLayer*)layer_object;
        }
        else if ([view.layer isKindOfClass:[CAMetalLayer class]])
        {
            layer = (CAMetalLayer*)view.layer;
        }
        else
        {
            // The view is the host's: it gets a layer of the engine's own on top of its content, and keeps everything
            // else (a view the engine made layer-backed goes back when the layer leaves).
            bool made_layer_backed = false;
            if (view.layer == nil)
            {
                view.wantsLayer = YES;
                made_layer_backed = true;
            }
            if (view.layer == nil)
            {
                if (made_layer_backed)
                {
                    view.wantsLayer = NO;  // a refused call changes nothing
                }
                *why = "the NSView has no layer to put a CAMetalLayer in";
                return false;
            }
            layer = [CAMetalLayer layer];
            layer.opaque = YES;
            layer.autoresizingMask = kCALayerWidthSizable |
                                     kCALayerHeightSizable;  // follows the view's bounds by itself
            FitSublayer(view, layer, scale);
            [view.layer addSublayer:layer];
            owned = true;
            out->made_layer_backed = made_layer_backed;
        }
        out->view = view_handle;
        out->layer = (__bridge_retained void*)layer;
        out->owned = owned;
        return true;
    }
}

void ResizeLayer(const CocoaLayer& bound, f32 scale)
{
    @autoreleasepool
    {
        if (!bound.owned || bound.layer == nullptr)
        {
            return;
        }
        FitSublayer((__bridge NSView*)bound.view, (__bridge CAMetalLayer*)bound.layer, scale);
    }
}

void DetachLayer(CocoaLayer* bound)
{
    @autoreleasepool
    {
        if (bound->layer == nullptr)
        {
            return;
        }
        CAMetalLayer* layer = (__bridge_transfer CAMetalLayer*)bound->layer;  // the reference AttachLayer took
        if (bound->owned)
        {
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            [layer removeFromSuperlayer];
            [CATransaction commit];
            if (bound->made_layer_backed)
            {
                ((__bridge NSView*)bound->view).wantsLayer = NO;
            }
        }
        layer = nil;
        *bound = CocoaLayer();
    }
}

}  // namespace engine_placeholder::engine_c::macos
