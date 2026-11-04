#include "rhi/uniform_ring.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi {

UniformRing::UniformRing(Device* device, u64 bytes_per_frame) : device_(device) {
    ENGINE_ASSERT(device != nullptr && bytes_per_frame > 0, "UniformRing: needs a device and a non-zero size");
    alignment_ = device->Caps().min_uniform_buffer_offset_alignment;
    if (alignment_ == 0) {
        alignment_ = 1;
    }
    region_bytes_ = (bytes_per_frame + alignment_ - 1) / alignment_ * alignment_;
    BufferDesc desc;
    desc.size = region_bytes_ * kSlots;
    desc.usage = BufferUsage::Uniform;
    desc.location = MemoryLocation::CpuToGpu;
    desc.debug_name = "uniform ring";
    buffer_ = device->CreateBuffer(desc);
    if (buffer_.IsValid()) {
        mapped_ = static_cast<unsigned char*>(device->MapBuffer(buffer_));
    }
}

UniformRing::~UniformRing() {
    if (buffer_.IsValid()) {
        if (mapped_ != nullptr) {
            device_->UnmapBuffer(buffer_);
        }
        device_->DestroyBuffer(buffer_);
    }
}

void UniformRing::BeginFrame() {
    const u64 frame = device_->FrameNumber();
    ENGINE_ASSERT(frame != 0, "UniformRing::BeginFrame: no frame has begun");
    region_base_ = (frame % kSlots) * region_bytes_;
    used_.store(0, std::memory_order_relaxed);
    reported_full_.store(false, std::memory_order_relaxed);
}

void UniformRing::Flush() {
    const u64 used = used_.load(std::memory_order_relaxed);
    if (mapped_ != nullptr && used > 0) {
        device_->FlushMappedBuffer(buffer_, region_base_, used < region_bytes_ ? used : region_bytes_);
    }
}

UniformAllocation UniformRing::Allocate(u64 size) {
    UniformAllocation out;
    ENGINE_ASSERT(size > 0, "UniformRing::Allocate: size must be greater than zero");
    if (mapped_ == nullptr || size == 0) {
        return out;
    }
    const u64 reserve = (size + alignment_ - 1) / alignment_ * alignment_;
    u64 at = used_.load(std::memory_order_relaxed);
    do {
        if (reserve > region_bytes_ || at > region_bytes_ - reserve) {
            if (!reported_full_.exchange(true, std::memory_order_relaxed)) {
                RhiLogV(LogVerbosity::Error, "UniformRing: the frame's region of %llu bytes is full",
                        static_cast<unsigned long long>(region_bytes_));
            }
            return out;
        }
    } while (!used_.compare_exchange_weak(at, at + reserve, std::memory_order_relaxed));
    out.buffer = buffer_;
    out.offset = region_base_ + at;
    out.size = size;
    out.data = mapped_ + out.offset;
    return out;
}

} // namespace engine_placeholder::rhi
