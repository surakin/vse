// MetalDevice: command lists and submission (Phase 26 Day 320).
//
// Lists come from a pool per frame slot and are recycled when the slot comes round (BeginFrame waited for that frame).
// Each list records into its own retained MTLCommandBuffer, so lists can be recorded on several jobs at once; Submit
// commits them in the order given, which is the order the queue runs them, then signals the timeline once after the
// last.

#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/metal/metal_command_list.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

CommandList* MetalDevice::CreateCommandList()
{
    ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Acquire),
                  "CreateCommandList: no frame is open (call BeginFrame first)");
    if (DeviceLost())
    {
        return nullptr;
    }
    const u64 frame = FrameNumber();
    LockGuard<Mutex> lock(list_mutex_);
    ListPool& pool = list_pools_[frame % kFrameSlots];
    if (pool.frame != frame)
    {
        pool.used = 0;  // the slot's previous frame has finished: its lists can be recorded again
        pool.frame = frame;
    }
    if (pool.used == pool.lists.Size())
    {
        UniquePtr<MetalCommandList> made = MakeUnique<MetalCommandList>(this);
        if (!made)
        {
            return nullptr;
        }
        pool.lists.Push(static_cast<UniquePtr<MetalCommandList>&&>(made));
    }
    MetalCommandList* list = pool.lists[pool.used].Get();
    list->Recycle(frame);
    ++pool.used;
    return list;
}

FenceValue MetalDevice::Submit(CommandList* const* lists, usize count)
{
    ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Acquire), "Submit: no frame is open (call BeginFrame first)");
    ENGINE_ASSERT(count == 0 || lists != nullptr, "Submit: lists is null but count is not zero");
    const u64 frame = FrameNumber();
    // Under the submission lock: the queue's order is the order of the commits, and two threads that wrongly submit one
    // list are serialized so the second one's check fails.
    LockGuard<Mutex> lock(submit_mutex_);
    for (usize i = 0; i < count; ++i)
    {
        auto* list = static_cast<MetalCommandList*>(lists[i]);
        ENGINE_ASSERT(list != nullptr && list->Owner() == this, "Submit: a list is null or belongs to another device");
        ENGINE_ASSERT(list->GetPhase() == MetalCommandList::Phase::Ended,
                      "Submit: a list was not ended, or was already submitted or listed twice");
        ENGINE_ASSERT(list->Frame() == frame, "Submit: a list is from an earlier frame");
        // Marked at once, so the same list later in this batch fails the check above (a second commit of one command
        // buffer would raise inside Metal).
        list->SetPhase(MetalCommandList::Phase::Submitted);
    }
    auto finish = [&]()
    {
        for (usize i = 0; i < count; ++i)
        {
            static_cast<MetalCommandList*>(lists[i])->SetPhase(MetalCommandList::Phase::Submitted);
        }
    };
    if (DeviceLost())
    {
        finish();
        return 0;
    }
    if (count == 0)
    {
        return last_signaled_;  // nothing new: the fence for everything submitted so far
    }
    if (lose_device_at_submit_ != 0 && submit_count_.FetchAdd(1, MemoryOrder::Relaxed) + 1 == lose_device_at_submit_ &&
        InjectLoss("submit", lose_device_at_submit_))
    {
        finish();
        return 0;
    }
    for (usize i = 0; i < count; ++i)
    {
        auto* list = static_cast<MetalCommandList*>(lists[i]);
        if (list->Failed() || list->Buffer() == nil)
        {
            RhiLog(LogVerbosity::Error,
                   "Submit: list %zu failed while recording and was not submitted (nothing in the batch ran)", i);
            finish();
            return 0;
        }
    }
    for (usize i = 0; i < count; ++i)
    {
        id<MTLCommandBuffer> buffer = static_cast<MetalCommandList*>(lists[i])->Buffer();
        WatchCommandBuffer(buffer);
        [buffer commit];
    }
    finish();
    // One signal after the batch: a separate command buffer, ordered after the lists by the queue.
    return SignalLocked();
}

id<MTLTexture> MetalDevice::TextureObject(TextureHandle texture, Format* format) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    *format = t != nullptr ? t->desc.format : Format::Undefined;
    return t != nullptr ? t->texture : nil;
}

Format MetalDevice::TextureFormat(TextureHandle texture) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    return t != nullptr ? t->desc.format : Format::Undefined;
}

}  // namespace engine_placeholder::rhi::metal
