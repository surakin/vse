#include "ui/layout.h"

#include "log/assert.h"

namespace engine_placeholder::ui {

namespace {

// The most rows or columns of a grid: a child that names a cell further out is placed in the last one, so a wild index cannot allocate without bound.
constexpr u32 kMaxTracks = 256;

f32 Max0(f32 v) { return v > 0 ? v : 0; }
f32 ClampF(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

Rect Shrink(const Rect& r, const Insets& in) {
    Rect out;
    out.x = r.x + in.left;
    out.y = r.y + in.top;
    out.w = Max0(r.w - in.Horizontal());
    out.h = Max0(r.h - in.Vertical());
    return out;
}

f32 Sub(f32 limit, f32 amount) { return limit >= kUnbounded ? kUnbounded : Max0(limit - amount); }

// Places `child` in `cell`. A fixed axis takes the cell's whole extent; otherwise the child has its alignment: stretch fills the cell (within the child's minimum and maximum, the
// preferred size is not used), the others use the preferred size. The height of a child that fills the width is measured at that width, so wrapping text is right.
Rect PlaceChild(Widget* child, const Rect& cell, bool fix_w, bool fix_h) {
    const LayoutProps& p = child->Props();
    f32 w;
    if (fix_w) {
        w = cell.w;
    } else if (p.align_x == Align::Stretch) {
        w = ClampF(cell.w, p.min.w, p.max.w);
    } else {
        Constraints c;
        c.max_w = cell.w;
        c.max_h = cell.h;
        w = child->MeasureTotal(c).w;
    }
    f32 h;
    if (fix_h) {
        h = cell.h;
    } else if (p.align_y == Align::Stretch) {
        h = ClampF(cell.h, p.min.h, p.max.h);
    } else {
        Constraints c;
        c.min_w = w;
        c.max_w = w;
        c.max_h = cell.h;
        h = child->MeasureTotal(c).h;
    }
    Rect r;
    r.w = w;
    r.h = h;
    const Align ax = fix_w ? Align::Start : (p.align_x == Align::Stretch ? Align::Start : p.align_x);
    const Align ay = fix_h ? Align::Start : (p.align_y == Align::Stretch ? Align::Start : p.align_y);
    r.x = cell.x + (ax == Align::Center ? (cell.w - w) * 0.5f : (ax == Align::End ? cell.w - w : 0.0f));
    r.y = cell.y + (ay == Align::Center ? (cell.h - h) * 0.5f : (ay == Align::End ? cell.h - h : 0.0f));
    return r;
}

} // namespace

// ---- BoxLayout --------------------------------------------------------------------------------------------------------------------------

BoxLayout::BoxLayout(Orientation orientation, StringView name) : Widget(name), orientation_(orientation) {}

void BoxLayout::SetSpacing(f32 spacing) {
    spacing_ = spacing;
    MarkDirty(kLayoutDirty);
}

void BoxLayout::SetPadding(const Insets& padding) {
    padding_ = padding;
    MarkDirty(kLayoutDirty);
}

Size BoxLayout::Measure(const Constraints& constraints) {
    const bool horizontal = orientation_ == Orientation::Horizontal;
    Constraints inner;
    inner.max_w = Sub(constraints.max_w, padding_.Horizontal());
    inner.max_h = Sub(constraints.max_h, padding_.Vertical());
    f32 main = 0, cross = 0;
    usize shown = 0;
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (child->Collapsed()) continue;
        Constraints c = inner;
        if (horizontal) c.max_w = kUnbounded; else c.max_h = kUnbounded;
        const Size s = child->MeasureTotal(c);
        main += horizontal ? s.w : s.h;
        const f32 x = horizontal ? s.h : s.w;
        if (x > cross) cross = x;
        ++shown;
    }
    if (shown > 1) main += spacing_ * static_cast<f32>(shown - 1);
    Size out = horizontal ? Size{main + padding_.Horizontal(), cross + padding_.Vertical()} : Size{cross + padding_.Horizontal(), main + padding_.Vertical()};
    return out;
}

void BoxLayout::Arrange(const Rect& rect) {
    SetBounds(rect);
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const Rect content = Shrink(rect, padding_);
    Vector<Widget*> items;
    for (usize i = 0; i < ChildCount(); ++i) {
        if (!ChildAt(i)->Collapsed()) items.Push(ChildAt(i));
    }
    const usize n = items.Size();
    if (n == 0) return;
    Vector<f32> size, lo, hi, weight;
    for (usize i = 0; i < n; ++i) {
        Widget* child = items[i];
        const LayoutProps& p = child->Props();
        Constraints c;
        if (horizontal) {
            c.max_h = content.h;
            if (p.align_y == Align::Stretch) {
                c.min_h = ClampF(content.h, p.min.h, p.max.h);
                c.max_h = c.min_h;
            }
        } else {
            c.max_w = content.w;
            if (p.align_x == Align::Stretch) {
                c.min_w = ClampF(content.w, p.min.w, p.max.w);
                c.max_w = c.min_w;
            }
        }
        const Size s = child->MeasureTotal(c);
        size.Push(horizontal ? s.w : s.h);
        lo.Push(horizontal ? p.min.w : p.min.h);
        hi.Push(horizontal ? p.max.w : p.max.h);
        weight.Push(p.weight > 0 ? p.weight : 0.0f);
    }
    const f32 avail = (horizontal ? content.w : content.h) - spacing_ * static_cast<f32>(n - 1);
    f32 used = 0;
    for (usize i = 0; i < n; ++i) used += size[i];
    if (avail > used) {
        // Free space goes to the weighted children, a child that reaches its maximum is frozen and the rest share what is left.
        Vector<bool> frozen;
        for (usize i = 0; i < n; ++i) frozen.Push(false);
        for (usize iter = 0; iter <= n; ++iter) {
            f32 now = 0, total_weight = 0;
            for (usize i = 0; i < n; ++i) {
                now += size[i];
                if (!frozen[i] && weight[i] > 0) total_weight += weight[i];
            }
            const f32 free = avail - now;
            if (free <= 1e-4f || total_weight <= 0) break;
            bool clamped = false;
            for (usize i = 0; i < n; ++i) {
                if (frozen[i] || weight[i] <= 0) continue;
                if (size[i] + free * weight[i] / total_weight > hi[i]) {
                    size[i] = hi[i] > size[i] ? hi[i] : size[i];
                    frozen[i] = true;
                    clamped = true;
                }
            }
            if (!clamped) {
                for (usize i = 0; i < n; ++i) {
                    if (!frozen[i] && weight[i] > 0) size[i] += free * weight[i] / total_weight;
                }
                break;
            }
        }
    } else if (avail < used) {
        // Too little room: every child gives up a share of what it has above its minimum.
        f32 shrinkable = 0;
        for (usize i = 0; i < n; ++i) shrinkable += Max0(size[i] - lo[i]);
        if (shrinkable > 0) {
            const f32 ratio = ClampF((used - avail) / shrinkable, 0.0f, 1.0f);
            for (usize i = 0; i < n; ++i) size[i] -= Max0(size[i] - lo[i]) * ratio;
        }
    }
    f32 pos = horizontal ? content.x : content.y;
    for (usize i = 0; i < n; ++i) {
        Rect cell;
        if (horizontal) {
            cell = Rect{pos, content.y, size[i], content.h};
        } else {
            cell = Rect{content.x, pos, content.w, size[i]};
        }
        items[i]->Arrange(PlaceChild(items[i], cell, horizontal, !horizontal));
        pos += size[i] + spacing_;
    }
}

// ---- StackLayout ------------------------------------------------------------------------------------------------------------------------

StackLayout::StackLayout(StringView name) : Widget(name) {}

void StackLayout::SetPadding(const Insets& padding) {
    padding_ = padding;
    MarkDirty(kLayoutDirty);
}

Size StackLayout::Measure(const Constraints& constraints) {
    Constraints inner;
    inner.max_w = Sub(constraints.max_w, padding_.Horizontal());
    inner.max_h = Sub(constraints.max_h, padding_.Vertical());
    Size out;
    for (usize i = 0; i < ChildCount(); ++i) {
        if (ChildAt(i)->Collapsed()) continue;
        const Size s = ChildAt(i)->MeasureTotal(inner);
        if (s.w > out.w) out.w = s.w;
        if (s.h > out.h) out.h = s.h;
    }
    out.w += padding_.Horizontal();
    out.h += padding_.Vertical();
    return out;
}

void StackLayout::Arrange(const Rect& rect) {
    SetBounds(rect);
    const Rect content = Shrink(rect, padding_);
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (!child->Collapsed()) child->Arrange(PlaceChild(child, content, false, false));
    }
}

// ---- AnchorLayout -----------------------------------------------------------------------------------------------------------------------

AnchorLayout::AnchorLayout(StringView name) : Widget(name) {}

void AnchorLayout::Arrange(const Rect& rect) {
    SetBounds(rect);
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (child->Collapsed()) continue;
        const AnchorSpec& a = child->Props().anchor;
        Rect r;
        r.x = rect.x + a.min_x * rect.w + a.offset_left;
        r.y = rect.y + a.min_y * rect.h + a.offset_top;
        Constraints c;
        c.max_w = rect.w;
        c.max_h = rect.h;
        if (a.max_x == a.min_x) {
            r.w = child->MeasureTotal(c).w;
        } else {
            r.w = Max0(rect.x + a.max_x * rect.w + a.offset_right - r.x);
        }
        if (a.max_y == a.min_y) {
            c.min_w = r.w;
            c.max_w = r.w;
            r.h = child->MeasureTotal(c).h;
        } else {
            r.h = Max0(rect.y + a.max_y * rect.h + a.offset_bottom - r.y);
        }
        child->Arrange(r);
    }
}

// ---- GridLayout -------------------------------------------------------------------------------------------------------------------------

GridLayout::GridLayout(StringView name) : Widget(name) {}

void GridLayout::AddColumn(TrackSize size) {
    columns_.Push(size);
    MarkDirty(kLayoutDirty);
}

void GridLayout::AddRow(TrackSize size) {
    rows_.Push(size);
    MarkDirty(kLayoutDirty);
}

void GridLayout::SetSpacing(f32 column_spacing, f32 row_spacing) {
    column_spacing_ = column_spacing;
    row_spacing_ = row_spacing;
    MarkDirty(kLayoutDirty);
}

void GridLayout::SetPadding(const Insets& padding) {
    padding_ = padding;
    MarkDirty(kLayoutDirty);
}

u32 GridLayout::ColumnCount() const {
    u32 n = static_cast<u32>(columns_.Size());
    for (usize i = 0; i < ChildCount(); ++i) {
        const LayoutProps& p = ChildAt(i)->Props();
        if (!ChildAt(i)->Collapsed() && p.column + 1 > n) n = p.column + 1;
    }
    return n > kMaxTracks ? kMaxTracks : n;
}

u32 GridLayout::RowCount() const {
    u32 n = static_cast<u32>(rows_.Size());
    for (usize i = 0; i < ChildCount(); ++i) {
        const LayoutProps& p = ChildAt(i)->Props();
        if (!ChildAt(i)->Collapsed() && p.row + 1 > n) n = p.row + 1;
    }
    return n > kMaxTracks ? kMaxTracks : n;
}

void GridLayout::Solve(f32 avail_w, f32 avail_h, Solved* out) {
    const u32 nc = ColumnCount();
    const u32 nr = RowCount();
    out->columns.Resize(nc);
    out->rows.Resize(nr);
    Vector<f32> col_pref, row_pref;
    col_pref.Resize(nc);
    row_pref.Resize(nr);
    for (u32 c = 0; c < nc; ++c) col_pref[c] = 0;
    for (u32 r = 0; r < nr; ++r) row_pref[r] = 0;
    auto track = [](const Vector<TrackSize>& v, u32 i) { return i < v.Size() ? v[i] : TrackSize::Auto(); };

    // Columns: preferred widths of the single-column children (a fixed track ignores them).
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (child->Collapsed() || child->Props().column_span != 1) continue;
        const u32 c = child->Props().column < nc ? child->Props().column : nc - 1;
        const Size s = child->MeasureTotal(Constraints());
        if (s.w > col_pref[c]) col_pref[c] = s.w;
    }
    f32 fixed_total = 0, weight_total = 0;
    for (u32 c = 0; c < nc; ++c) {
        const TrackSize t = track(columns_, c);
        if (t.kind == TrackSize::Kind::Fixed) col_pref[c] = t.value;
        out->columns[c] = col_pref[c];
        fixed_total += col_pref[c];
        if (t.kind == TrackSize::Kind::Weight) weight_total += t.value > 0 ? t.value : 0;
    }
    const f32 col_gaps = nc > 1 ? column_spacing_ * static_cast<f32>(nc - 1) : 0.0f;
    if (avail_w < kUnbounded && weight_total > 0) {
        const f32 free = avail_w - col_gaps - fixed_total;
        for (u32 c = 0; c < nc; ++c) {
            const TrackSize t = track(columns_, c);
            if (t.kind != TrackSize::Kind::Weight) continue;
            if (free > 0) out->columns[c] = col_pref[c] + free * (t.value > 0 ? t.value : 0) / weight_total;
        }
    }
    // Rows: heights of the single-row children measured at their column's width.
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (child->Collapsed() || child->Props().row_span != 1) continue;
        const LayoutProps& p = child->Props();
        const u32 r = p.row < nr ? p.row : nr - 1;
        const u32 first_col = p.column < nc ? p.column : nc - 1;
        f32 w = 0;
        for (u32 k = 0; k < p.column_span && first_col + k < nc; ++k) w += out->columns[first_col + k] + (k > 0 ? column_spacing_ : 0.0f);
        Constraints c;
        c.min_w = w;
        c.max_w = w;
        const Size s = child->MeasureTotal(c);
        if (s.h > row_pref[r]) row_pref[r] = s.h;
    }
    fixed_total = 0;
    weight_total = 0;
    for (u32 r = 0; r < nr; ++r) {
        const TrackSize t = track(rows_, r);
        if (t.kind == TrackSize::Kind::Fixed) row_pref[r] = t.value;
        out->rows[r] = row_pref[r];
        fixed_total += row_pref[r];
        if (t.kind == TrackSize::Kind::Weight) weight_total += t.value > 0 ? t.value : 0;
    }
    const f32 row_gaps = nr > 1 ? row_spacing_ * static_cast<f32>(nr - 1) : 0.0f;
    if (avail_h < kUnbounded && weight_total > 0) {
        const f32 free = avail_h - row_gaps - fixed_total;
        for (u32 r = 0; r < nr; ++r) {
            const TrackSize t = track(rows_, r);
            if (t.kind != TrackSize::Kind::Weight) continue;
            if (free > 0) out->rows[r] = row_pref[r] + free * (t.value > 0 ? t.value : 0) / weight_total;
        }
    }
}

Size GridLayout::Measure(const Constraints&) {
    Solved solved;
    Solve(kUnbounded, kUnbounded, &solved);
    Size out;
    for (usize c = 0; c < solved.columns.Size(); ++c) out.w += solved.columns[c];
    for (usize r = 0; r < solved.rows.Size(); ++r) out.h += solved.rows[r];
    if (solved.columns.Size() > 1) out.w += column_spacing_ * static_cast<f32>(solved.columns.Size() - 1);
    if (solved.rows.Size() > 1) out.h += row_spacing_ * static_cast<f32>(solved.rows.Size() - 1);
    out.w += padding_.Horizontal();
    out.h += padding_.Vertical();
    return out;
}

void GridLayout::Arrange(const Rect& rect) {
    SetBounds(rect);
    const Rect content = Shrink(rect, padding_);
    Solved solved;
    Solve(content.w, content.h, &solved);
    Vector<f32> col_x, row_y;
    f32 x = content.x;
    for (usize c = 0; c < solved.columns.Size(); ++c) {
        col_x.Push(x);
        x += solved.columns[c] + column_spacing_;
    }
    f32 y = content.y;
    for (usize r = 0; r < solved.rows.Size(); ++r) {
        row_y.Push(y);
        y += solved.rows[r] + row_spacing_;
    }
    for (usize i = 0; i < ChildCount(); ++i) {
        Widget* child = ChildAt(i);
        if (child->Collapsed()) continue;
        const LayoutProps& p = child->Props();
        const u32 c0 = p.column < solved.columns.Size() ? p.column : static_cast<u32>(solved.columns.Size()) - 1;
        const u32 r0 = p.row < solved.rows.Size() ? p.row : static_cast<u32>(solved.rows.Size()) - 1;
        u32 c1 = c0 + (p.column_span > 0 ? (p.column_span < kMaxTracks ? p.column_span : kMaxTracks) : 1) - 1;
        u32 r1 = r0 + (p.row_span > 0 ? (p.row_span < kMaxTracks ? p.row_span : kMaxTracks) : 1) - 1;
        if (c1 >= solved.columns.Size()) c1 = static_cast<u32>(solved.columns.Size()) - 1;
        if (r1 >= solved.rows.Size()) r1 = static_cast<u32>(solved.rows.Size()) - 1;
        Rect cell;
        cell.x = col_x[c0];
        cell.y = row_y[r0];
        cell.w = col_x[c1] + solved.columns[c1] - col_x[c0];
        cell.h = row_y[r1] + solved.rows[r1] - row_y[r0];
        child->Arrange(PlaceChild(child, cell, false, false));
    }
}

// ---- Spacer -----------------------------------------------------------------------------------------------------------------------------

Spacer::Spacer(f32 weight) : Widget(StringView()) {
    LayoutProps p;
    p.weight = weight;
    SetProps(p);
}

} // namespace engine_placeholder::ui
