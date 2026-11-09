#include "ui/testing/software_raster.h"

#include <cmath>

#include "log/assert.h"

namespace engine_placeholder::ui {

namespace {

struct Float4 {
    f32 r = 0, g = 0, b = 0, a = 0;
};

f32 Saturate(f32 x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

f32 SmoothStep(f32 e0, f32 e1, f32 x) {
    const f32 t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

f32 BoxDistance(f32 px, f32 py, f32 hw, f32 hh, f32 radius) {
    const f32 r = std::fmin(radius, std::fmin(hw, hh));
    const f32 qx = std::fabs(px) - hw + r;
    const f32 qy = std::fabs(py) - hh + r;
    const f32 ox = qx > 0 ? qx : 0;
    const f32 oy = qy > 0 ? qy : 0;
    return std::sqrt(ox * ox + oy * oy) + std::fmin(std::fmax(qx, qy), 0.0f) - r;
}

// The edge function of the triangle edge a->b at p; the sign says on which side p lies.
f64 Edge(f64 ax, f64 ay, f64 bx, f64 by, f64 px, f64 py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

// The tie rule of a pixel centre exactly on an edge: the edge a->b owns it when it is a "top or left" edge, so a shared edge is drawn once.
bool OwnsTie(f64 ax, f64 ay, f64 bx, f64 by) {
    const f64 dx = bx - ax;
    const f64 dy = by - ay;
    return dy > 0 || (dy == 0 && dx < 0);
}

} // namespace

const u8* RasterImage::Pixel(u32 x, u32 y) const {
    ENGINE_ASSERT(x < width && y < height, "RasterImage::Pixel: outside the image");
    return rgba.Data() + (static_cast<usize>(y) * width + x) * 4;
}

void RasterizeDrawList(const DrawList& list, const text::GlyphAtlas* atlas, u32 width, u32 height, u32 clear_abgr, RasterImage* out) {
    ENGINE_ASSERT(out != nullptr && width > 0 && height > 0, "RasterizeDrawList: bad arguments");
    out->width = width;
    out->height = height;
    out->rgba.Resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < static_cast<usize>(width) * height; ++i) {
        out->rgba[i * 4 + 0] = static_cast<u8>(clear_abgr & 0xFF);
        out->rgba[i * 4 + 1] = static_cast<u8>((clear_abgr >> 8) & 0xFF);
        out->rgba[i * 4 + 2] = static_cast<u8>((clear_abgr >> 16) & 0xFF);
        out->rgba[i * 4 + 3] = static_cast<u8>((clear_abgr >> 24) & 0xFF);
    }

    for (usize c = 0; c < list.commands.Size(); ++c) {
        const DrawCommand& cmd = list.commands[c];
        if (cmd.texture.kind == TextureKind::Image) continue;
        const u8* page = nullptr;
        u32 page_size = 0;
        if (cmd.texture.kind == TextureKind::Atlas) {
            if (atlas == nullptr || cmd.texture.id >= atlas->PageCount()) continue;
            page = atlas->PagePixels(cmd.texture.id).Data();
            page_size = atlas->PageSize();
        }
        i64 cx0 = cmd.clip.x < 0 ? 0 : cmd.clip.x;
        i64 cy0 = cmd.clip.y < 0 ? 0 : cmd.clip.y;
        i64 cx1 = static_cast<i64>(cmd.clip.x) + cmd.clip.w;
        i64 cy1 = static_cast<i64>(cmd.clip.y) + cmd.clip.h;
        if (cx1 > width) cx1 = width;
        if (cy1 > height) cy1 = height;
        if (cx0 >= cx1 || cy0 >= cy1) continue;

        for (u32 t = 0; t + 2 < cmd.index_count; t += 3) {
            const Vertex* v[3] = {&list.vertices[list.indices[cmd.first_index + t]], &list.vertices[list.indices[cmd.first_index + t + 1]],
                                  &list.vertices[list.indices[cmd.first_index + t + 2]]};
            f64 area = Edge(v[0]->x, v[0]->y, v[1]->x, v[1]->y, v[2]->x, v[2]->y);
            if (area == 0) continue;
            if (area < 0) { // make the winding the same for every triangle
                const Vertex* tmp = v[1];
                v[1] = v[2];
                v[2] = tmp;
                area = -area;
            }
            f64 minx = v[0]->x, maxx = v[0]->x, miny = v[0]->y, maxy = v[0]->y;
            for (int k = 1; k < 3; ++k) {
                if (v[k]->x < minx) minx = v[k]->x;
                if (v[k]->x > maxx) maxx = v[k]->x;
                if (v[k]->y < miny) miny = v[k]->y;
                if (v[k]->y > maxy) maxy = v[k]->y;
            }
            i64 x0 = static_cast<i64>(std::floor(minx)), x1 = static_cast<i64>(std::ceil(maxx));
            i64 y0 = static_cast<i64>(std::floor(miny)), y1 = static_cast<i64>(std::ceil(maxy));
            if (x0 < cx0) x0 = cx0;
            if (y0 < cy0) y0 = cy0;
            if (x1 > cx1) x1 = cx1;
            if (y1 > cy1) y1 = cy1;
            const u32 flags = v[0]->flags;
            const u32 mode = flags & kModeMask;
            const u32 color = v[0]->color;
            for (i64 y = y0; y < y1; ++y) {
                for (i64 x = x0; x < x1; ++x) {
                    const f64 px = static_cast<f64>(x) + 0.5;
                    const f64 py = static_cast<f64>(y) + 0.5;
                    f64 w[3];
                    bool inside = true;
                    for (int k = 0; k < 3 && inside; ++k) {
                        const Vertex* a = v[(k + 1) % 3];
                        const Vertex* b = v[(k + 2) % 3];
                        w[k] = Edge(a->x, a->y, b->x, b->y, px, py);
                        if (w[k] < 0 || (w[k] == 0 && !OwnsTie(a->x, a->y, b->x, b->y))) inside = false;
                    }
                    if (!inside) continue;
                    const f32 l0 = static_cast<f32>(w[0] / area), l1 = static_cast<f32>(w[1] / area), l2 = static_cast<f32>(w[2] / area);
                    auto lerp = [&](f32 a, f32 b, f32 c2) { return a * l0 + b * l1 + c2 * l2; };
                    const f32 u = lerp(v[0]->u, v[1]->u, v[2]->u);
                    const f32 vv = lerp(v[0]->v, v[1]->v, v[2]->v);
                    f32 alpha = static_cast<f32>((color >> 24) & 0xFF) / 255.0f;
                    const f32 r = static_cast<f32>(color & 0xFF) / 255.0f;
                    const f32 g = static_cast<f32>((color >> 8) & 0xFF) / 255.0f;
                    const f32 b = static_cast<f32>((color >> 16) & 0xFF) / 255.0f;
                    if (mode == kModeRect) {
                        const f32 hw = lerp(v[0]->half_w, v[1]->half_w, v[2]->half_w);
                        const f32 hh = lerp(v[0]->half_h, v[1]->half_h, v[2]->half_h);
                        const f32 rad = lerp(v[0]->radius, v[1]->radius, v[2]->radius);
                        const f32 border = lerp(v[0]->border, v[1]->border, v[2]->border);
                        const f32 d = BoxDistance(u, vv, hw, hh, rad);
                        if ((flags & kFlagShadow) != 0) {
                            const f32 blur = std::fmax(border, 1.0f);
                            alpha *= 1.0f - SmoothStep(0.0f, blur, std::fmax(d, 0.0f));
                        } else {
                            const f32 outer = Saturate(0.5f - d);
                            if ((flags & kFlagRing) != 0) {
                                const f32 inner = Saturate(0.5f - (d + border));
                                alpha *= outer * (1.0f - inner);
                            } else {
                                alpha *= outer;
                            }
                        }
                    } else if (mode == kModeGlyph) {
                        if (page == nullptr) continue;
                        i64 tx = static_cast<i64>(std::floor(u * static_cast<f32>(page_size)));
                        i64 ty = static_cast<i64>(std::floor(vv * static_cast<f32>(page_size)));
                        if (tx < 0) tx = 0;
                        if (ty < 0) ty = 0;
                        if (tx >= page_size) tx = page_size - 1;
                        if (ty >= page_size) ty = page_size - 1;
                        alpha *= static_cast<f32>(page[static_cast<usize>(ty) * page_size + static_cast<usize>(tx)]) / 255.0f;
                    }
                    u8* dst = out->rgba.Data() + (static_cast<usize>(y) * width + static_cast<usize>(x)) * 4;
                    const f32 src[4] = {r * alpha, g * alpha, b * alpha, alpha};
                    for (int ch = 0; ch < 4; ++ch) {
                        const f32 d_old = static_cast<f32>(dst[ch]) / 255.0f;
                        const f32 mixed = src[ch] + d_old * (1.0f - alpha);
                        dst[ch] = static_cast<u8>(Saturate(mixed) * 255.0f + 0.5f);
                    }
                }
            }
        }
    }
}

} // namespace engine_placeholder::ui
