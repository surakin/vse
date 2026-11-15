#include "platform/foreign_window.h"

namespace engine_placeholder::os
{

ForeignWindow::ForeignWindow(const NativeWindowInfo& info, u32 width, u32 height, f32 scale)
    : info_(info), width_(width), height_(height), scale_(scale), serial_(detail::NextResizeSerial())
{
}

void ForeignWindow::SetSize(u32 width, u32 height, f32 scale)
{
    if (width != width_ || height != height_)
    {
        serial_ = detail::NextResizeSerial();
    }
    width_ = width;
    height_ = height;
    scale_ = scale;
}

void ForeignWindow::Rebind(const NativeWindowInfo& info, u32 width, u32 height, f32 scale)
{
    info_ = info;
    width_ = width;
    height_ = height;
    scale_ = scale;
    serial_ = detail::NextResizeSerial();
    valid_ = true;
}

}  // namespace engine_placeholder::os
