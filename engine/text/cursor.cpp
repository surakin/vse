#include "text/cursor.h"

#include "log/assert.h"

namespace engine_placeholder::text {

namespace {

// The index of the cluster that holds `offset` (the last one starting at or before it), or the cluster count for an offset at or past the end.
usize ClusterAt(const TextLayout& layout, u32 offset) {
    const usize n = layout.clusters.Size();
    if (n == 0 || offset >= layout.byte_length) return n;
    usize low = 0;
    usize high = n; // invariant: clusters[low].byte_begin <= offset < clusters[high].byte_begin
    while (high - low > 1) {
        const usize mid = (low + high) / 2;
        if (layout.clusters[mid].byte_begin <= offset) low = mid;
        else high = mid;
    }
    return low;
}

u32 LineOf(const TextLayout& layout, u32 offset) {
    const usize c = ClusterAt(layout, offset);
    if (c < layout.clusters.Size()) return layout.clusters[c].line;
    return static_cast<u32>(layout.lines.Size() - 1);
}

// The end boundary of line `l`'s visible text (see LineEnd).
u32 LineEndOf(const TextLayout& layout, u32 l) {
    const LineInfo& line = layout.lines[l];
    if (line.cluster_count == 0) return line.byte_begin;
    const usize first = line.first_cluster;
    const usize last = first + line.cluster_count - 1;
    if (layout.clusters[last].kind == ClusterKind::Break) return layout.clusters[last].byte_begin;
    if (l + 1 == layout.lines.Size()) return line.byte_end; // the last line ends with the text
    usize k = last + 1; // start of the run of trailing spaces and tabs
    while (k > first && (layout.clusters[k - 1].kind == ClusterKind::Space || layout.clusters[k - 1].kind == ClusterKind::Tab)) --k;
    return k == last + 1 ? line.byte_end : layout.clusters[k].byte_begin;
}

} // namespace

CursorPosition PositionOfOffset(const TextLayout& layout, u32 byte_offset) {
    CursorPosition pos;
    if (layout.lines.Size() == 0) return pos;
    const usize c = ClusterAt(layout, byte_offset);
    u32 l;
    if (c < layout.clusters.Size()) {
        l = layout.clusters[c].line;
        pos.x = layout.clusters[c].x;
    } else {
        l = static_cast<u32>(layout.lines.Size() - 1);
        const LineInfo& last = layout.lines[l];
        if (last.cluster_count == 0) {
            pos.x = last.x;
        } else {
            const ClusterInfo& tail = layout.clusters[last.first_cluster + last.cluster_count - 1];
            pos.x = tail.x + tail.width;
        }
    }
    pos.line = l;
    pos.y = layout.lines[l].top;
    pos.height = layout.lines[l].height;
    return pos;
}

u32 OffsetAtPoint(const TextLayout& layout, f32 x, f32 y) {
    if (layout.lines.Size() == 0) return 0;
    usize l = 0;
    const f32 height = layout.lines[0].height;
    if (y > 0 && height > 0) { // NaN and negative y stay on the first line
        const f32 row = y / height;
        l = row >= static_cast<f32>(layout.lines.Size()) ? layout.lines.Size() - 1 : static_cast<usize>(row); // compared before the cast: huge y is not converted
    }
    const LineInfo& line = layout.lines[l];
    const u32 end = LineEndOf(layout, static_cast<u32>(l));
    for (u32 k = 0; k < line.cluster_count; ++k) {
        const ClusterInfo& c = layout.clusters[line.first_cluster + k];
        if (c.byte_begin >= end) break;
        if (x <= c.x) return c.byte_begin; // also covers a cluster with no width (a lone combining mark)
        if (x < c.x + c.width) return x < c.x + c.width * 0.5f ? c.byte_begin : (c.byte_end < end ? c.byte_end : end);
    }
    return end;
}

u32 NextBoundary(const TextLayout& layout, u32 byte_offset) {
    const usize c = ClusterAt(layout, byte_offset);
    return c < layout.clusters.Size() ? layout.clusters[c].byte_end : layout.byte_length;
}

u32 PreviousBoundary(const TextLayout& layout, u32 byte_offset) {
    if (byte_offset == 0 || layout.clusters.Size() == 0) return 0;
    const usize c = ClusterAt(layout, byte_offset);
    const usize at = c < layout.clusters.Size() ? c : layout.clusters.Size(); // the cluster starting at or before `byte_offset`
    if (at < layout.clusters.Size() && layout.clusters[at].byte_begin < byte_offset) return layout.clusters[at].byte_begin; // inside a cluster: its start
    return at == 0 ? 0 : layout.clusters[at - 1].byte_begin;
}

u32 LineStart(const TextLayout& layout, u32 byte_offset) {
    if (layout.lines.Size() == 0) return 0;
    return layout.lines[LineOf(layout, byte_offset)].byte_begin;
}

u32 LineEnd(const TextLayout& layout, u32 byte_offset) {
    if (layout.lines.Size() == 0) return 0;
    return LineEndOf(layout, LineOf(layout, byte_offset));
}

u32 LineAbove(const TextLayout& layout, u32 byte_offset, f32 preferred_x) {
    if (layout.lines.Size() == 0) return 0;
    const u32 l = LineOf(layout, byte_offset);
    if (l == 0) return 0;
    const LineInfo& target = layout.lines[l - 1];
    return OffsetAtPoint(layout, preferred_x, target.top + target.height * 0.5f);
}

u32 LineBelow(const TextLayout& layout, u32 byte_offset, f32 preferred_x) {
    if (layout.lines.Size() == 0) return 0;
    const u32 l = LineOf(layout, byte_offset);
    if (l + 1 >= layout.lines.Size()) return layout.byte_length;
    const LineInfo& target = layout.lines[l + 1];
    return OffsetAtPoint(layout, preferred_x, target.top + target.height * 0.5f);
}

Status SelectionRects(const TextLayout& layout, u32 begin, u32 end, Vector<SelectionRect>* out) {
    ENGINE_ASSERT(out != nullptr, "SelectionRects: out is null");
    out->Clear();
    if (layout.lines.Size() == 0) return Status{};
    if (begin > end) {
        const u32 t = begin;
        begin = end;
        end = t;
    }
    const CursorPosition a = PositionOfOffset(layout, begin);
    const CursorPosition b = PositionOfOffset(layout, end);
    if (a.line == b.line && a.x == b.x) return Status{};
    if (!out->TryReserve(b.line - a.line + 1)) return Status::Of(Error::OutOfMemory, "out of memory collecting selection rectangles");
    for (u32 l = a.line; l <= b.line; ++l) {
        const LineInfo& line = layout.lines[l];
        const f32 from = l == a.line ? a.x : line.x;
        f32 to;
        if (l == b.line) {
            to = b.x;
        } else if (line.cluster_count == 0) {
            to = line.x;
        } else {
            const ClusterInfo& tail = layout.clusters[line.first_cluster + line.cluster_count - 1];
            to = tail.x + tail.width; // a break has no width, so the break itself is not painted
        }
        if (to <= from) continue;
        out->Push(SelectionRect{from, line.top, to - from, line.height});
    }
    return Status{};
}

} // namespace engine_placeholder::text
