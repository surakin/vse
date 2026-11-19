// morph.comp: applies a mesh's morph targets to its vertices (glTF coverage Day 7; renderer::GBufferPass). One thread
// per vertex: it copies the base vertex, every word of it, into the output, then adds the weighted deltas of up to
// eight active targets to the position, normal and tangent. A target's deltas are sorted by vertex, so the thread finds
// its own by a binary search; vertices a target does not move have no delta. No two threads write the same vertex, so
// one dispatch per object needs no atomics and no barrier between targets.
//
// Math (per vertex v; f32 attributes, the full layout)
//   out = base[v]; for each active target t with weight w: d = the delta of t for v, if any;
//   out.position += w * d.position; out.normal += w * d.normal; out.tangent.xyz += w * d.tangent
//   (the normal and the tangent are not renormalized here: the G-buffer shaders normalize them)
//
// Interface
//   set 0, binding 0: StructuredBuffer<uint> source: the base vertices (vertex_count * stride_words words) followed by
//                     the deltas (10 words each: vertex, position xyz, normal xyz, tangent xyz)
//   set 0, binding 1: RWStructuredBuffer<uint> output: this object's vertices start at out_base_word
//   push constants (128 bytes): uint vertex_count, stride_words, position_word, normal_word, tangent_word,
//       delta_base_word, out_base_word, target_count; then eight uint4 (first delta, delta count, weight bits, 0)

#include "target_common.slang"  // ENGINE_REGISTER

struct Push {
    uint vertex_count;
    uint stride_words;
    uint position_word;
    uint normal_word;
    uint tangent_word;
    uint delta_base_word;
    uint out_base_word;
    uint target_count;
    uint4 targets[8];  // x first delta, y delta count, z the weight as float bits
};

[[vk::binding(0, 0)]] StructuredBuffer<uint> source ENGINE_REGISTER(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output ENGINE_REGISTER(u1, space0);
[[vk::push_constant]] ConstantBuffer<Push> push ENGINE_REGISTER(b0, space15);

static const uint kDeltaWords = 10;

// The index of `vertex`'s delta among `count` deltas from `first`, or 0xffffffff when the target does not move it.
uint FindDelta(uint first, uint count, uint vertex) {
    uint lo = 0;
    uint hi = count;
    while (lo < hi) {
        uint mid = (lo + hi) / 2;
        uint at = source[push.delta_base_word + (first + mid) * kDeltaWords];
        if (at == vertex) {
            return first + mid;
        }
        if (at < vertex) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0xffffffffu;
}

void Add(uint out_word, uint delta_word, float w) {
    for (uint c = 0; c < 3; ++c) {
        float value = asfloat(output[out_word + c]) + w * asfloat(source[delta_word + c]);
        output[out_word + c] = asuint(value);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint v = id.x;
    if (v >= push.vertex_count) {
        return;
    }
    uint base = v * push.stride_words;
    uint out_base = push.out_base_word + base;
    for (uint k = 0; k < push.stride_words; ++k) {
        output[out_base + k] = source[base + k];
    }
    for (uint t = 0; t < push.target_count && t < 8u; ++t) {
        uint4 target = push.targets[t];
        uint d = FindDelta(target.x, target.y, v);
        if (d == 0xffffffffu) {
            continue;
        }
        float w = asfloat(target.z);
        uint word = push.delta_base_word + d * kDeltaWords;
        Add(out_base + push.position_word, word + 1, w);
        Add(out_base + push.normal_word, word + 4, w);
        Add(out_base + push.tangent_word, word + 7, w);
    }
}
