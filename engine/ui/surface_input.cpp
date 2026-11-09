#include "ui/surface_input.h"

#include <cmath>

namespace engine_placeholder::ui {

namespace {

f32 Dot(const f32* a, const f32* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Cross(const f32* a, const f32* b, f32* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

} // namespace

SurfaceHit RayToSurfacePoint(const Ray3& ray, const SurfaceQuad& quad) {
    SurfaceHit result;
    // The front face looks along v x u: with u to the right and v down, the viewer stands on that side.
    f32 normal[3];
    Cross(quad.edge_v, quad.edge_u, normal);
    const f32 facing = Dot(ray.direction, normal);
    if (!(std::fabs(facing) > 1e-12f)) return result; // parallel, a zero direction or a degenerate quad
    f32 to_corner[3] = {quad.corner[0] - ray.origin[0], quad.corner[1] - ray.origin[1], quad.corner[2] - ray.origin[2]};
    const f32 t = Dot(to_corner, normal) / facing;
    if (!(t >= 0)) return result; // the quad is behind the ray
    f32 w[3] = {ray.origin[0] + ray.direction[0] * t - quad.corner[0], ray.origin[1] + ray.direction[1] * t - quad.corner[1],
                ray.origin[2] + ray.direction[2] * t - quad.corner[2]};
    // Solve w = u * edge_u + v * edge_v in the plane.
    const f32 uu = Dot(quad.edge_u, quad.edge_u);
    const f32 uv = Dot(quad.edge_u, quad.edge_v);
    const f32 vv = Dot(quad.edge_v, quad.edge_v);
    const f32 wu = Dot(w, quad.edge_u);
    const f32 wv = Dot(w, quad.edge_v);
    const f32 det = uu * vv - uv * uv;
    if (!(std::fabs(det) > 1e-20f)) return result;
    const f32 u = (wu * vv - wv * uv) / det;
    const f32 v = (wv * uu - wu * uv) / det;
    if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return result;
    result.t = t;
    if (facing > 0) {
        result.back_face = true; // the ray travels along the front normal: it came from behind
        return result;
    }
    result.hit = true;
    result.u = u;
    result.v = v;
    return result;
}

void SurfaceInput::SetInteractive(bool interactive) {
    if (interactive == interactive_) return;
    if (!interactive) Lost();
    interactive_ = interactive;
}

Point SurfaceInput::ToLogical(f32 x, f32 y) const {
    const f32 scale = surface_->Scale();
    return Point(x / scale, y / scale);
}

void SurfaceInput::Post(Event& e) { surface_->Post(e); }

void SurfaceInput::PointerMove(f32 x, f32 y, u32 modifiers) {
    if (!interactive_) return;
    Event e;
    e.type = EventType::PointerMove;
    e.position = ToLogical(x, y);
    e.modifiers = modifiers;
    last_ = e.position;
    Post(e);
}

void SurfaceInput::PointerMoveUv(f32 u, f32 v, u32 modifiers) {
    PointerMove(u * surface_->Width() * surface_->Scale(), v * surface_->Height() * surface_->Scale(), modifiers);
}

void SurfaceInput::PointerButton(f32 x, f32 y, u32 button, bool down, u32 modifiers) {
    if (!interactive_) return;
    Event e;
    e.type = down ? EventType::PointerDown : EventType::PointerUp;
    e.button = button;
    e.position = ToLogical(x, y);
    e.modifiers = modifiers;
    last_ = e.position;
    buttons_ = down ? (buttons_ | button) : (buttons_ & ~button);
    Post(e);
}

void SurfaceInput::PointerButtonUv(f32 u, f32 v, u32 button, bool down, u32 modifiers) {
    PointerButton(u * surface_->Width() * surface_->Scale(), v * surface_->Height() * surface_->Scale(), button, down, modifiers);
}

void SurfaceInput::Wheel(f32 dx, f32 dy, u32 modifiers) {
    if (!interactive_) return;
    Event e;
    e.type = EventType::Wheel;
    e.position = last_;
    e.wheel_x = dx;
    e.wheel_y = dy;
    e.modifiers = modifiers;
    Post(e);
}

void SurfaceInput::Key(ui::Key key, bool down, bool repeat, u32 modifiers) {
    if (!interactive_) return;
    Event e;
    e.type = down ? EventType::KeyDown : EventType::KeyUp;
    e.key = key;
    e.repeat = repeat;
    e.modifiers = modifiers;
    Post(e);
}

void SurfaceInput::Text(StringView utf8) {
    if (!interactive_) return;
    usize i = 0;
    while (i < utf8.Length()) {
        const u8 lead = static_cast<u8>(utf8[i]);
        usize len = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
        if (i + len > utf8.Length()) len = utf8.Length() - i;
        Event e;
        e.type = EventType::Text;
        e.text_length = static_cast<u8>(len);
        for (usize k = 0; k < len && k < 4; ++k) e.text[k] = utf8[i + k];
        Post(e);
        i += len;
    }
}

void SurfaceInput::Focus(bool focused) {
    if (!interactive_) return;
    Event e;
    e.type = focused ? EventType::WindowFocusGained : EventType::WindowFocusLost;
    Post(e);
}

void SurfaceInput::Lost() {
    if (buttons_ != 0) {
        for (u32 bit = 1; bit != 0; bit <<= 1) {
            if ((buttons_ & bit) == 0) continue;
            Event e;
            e.type = EventType::PointerUp;
            e.button = bit;
            e.position = last_;
            Post(e);
        }
        buttons_ = 0;
    }
    Event leave;
    leave.type = EventType::PointerLeave;
    Post(leave);
}

} // namespace engine_placeholder::ui
