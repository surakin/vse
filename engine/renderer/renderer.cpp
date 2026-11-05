#include "renderer/renderer.h"

#include <cstdarg>
#include <cstdio>

#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

const char* FrameStatusName(FrameStatus status) {
    switch (status) {
    case FrameStatus::Ok: return "ok";
    case FrameStatus::Skipped: return "skipped";
    case FrameStatus::DeviceLost: return "device-lost";
    case FrameStatus::Failed: return "failed";
    }
    return "?";
}

namespace {

// Checked before any member is built from the description.
const RendererDesc& Checked(const RendererDesc& desc) {
    ENGINE_ASSERT(desc.device != nullptr, "Renderer: the device is null");
    ENGINE_ASSERT(desc.build != nullptr, "Renderer: the frame callback is null");
    return desc;
}

} // namespace

Renderer::Renderer(const RendererDesc& desc) : desc_(Checked(desc)), arena_(desc.arena_bytes), pool_(desc.device, desc.pool) {}

void Renderer::SetError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_, sizeof(error_), fmt, ap);
    va_end(ap);
}

// Ends a frame that did not render. With an acquired swapchain image the image still has to be presented, or the next
// Acquire would find the previous one outstanding; an image that was never drawn is moved to Present and shown as it is.
FrameStatus Renderer::Abandon(FrameStatus status, bool acquired) {
    rhi::Device* device = desc_.device;
    if (acquired && !device->DeviceLost()) {
        rhi::CommandList* list = device->CreateCommandList();
        if (list != nullptr) {
            list->Begin();
            list->Barrier(desc_.swapchain->CurrentTexture(), rhi::ResourceState::Undefined, rhi::ResourceState::Present);
            list->End();
            rhi::CommandList* lists[] = {list};
            device->Submit(lists, 1);
        }
    }
    pool_.EndFrame();
    device->EndFrame();
    if (acquired) {
        const rhi::SwapchainStatus presented = desc_.swapchain->Present();
        if (presented == rhi::SwapchainStatus::Lost) status = FrameStatus::DeviceLost;
        else if (presented == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
    }
    if (device->DeviceLost()) status = FrameStatus::DeviceLost;
    if (status == FrameStatus::Failed) ++stats_.frames_failed;
    else if (status == FrameStatus::DeviceLost) ++stats_.frames_lost;
    return status;
}

FrameStatus Renderer::RenderFrame(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen) {
    rhi::Device* device = desc_.device;
    error_[0] = 0;
    if (desc_.swapchain == nullptr) {
        ENGINE_ASSERT(offscreen != nullptr && offscreen->texture.IsValid(), "Renderer::RenderFrame: no swapchain and no offscreen target");
    }
    ++frame_;
    device->BeginFrame();
    pool_.BeginFrame(frame_);
    if (device->DeviceLost()) {
        SetError("the device is lost");
        return Abandon(FrameStatus::DeviceLost, false);
    }

    FrameTarget target;
    bool acquired = false;
    if (desc_.swapchain != nullptr) {
        const rhi::SwapchainStatus status = desc_.swapchain->Acquire();
        if (status == rhi::SwapchainStatus::Lost) {
            SetError("the swapchain is lost");
            return Abandon(FrameStatus::DeviceLost, false);
        }
        if (status == rhi::SwapchainStatus::Minimized) {
            ++stats_.frames_skipped;
            Abandon(FrameStatus::Skipped, false);
            return FrameStatus::Skipped;
        }
        if (status == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
        acquired = true;
        target.texture = desc_.swapchain->CurrentTexture();
        target.width = desc_.swapchain->Width();
        target.height = desc_.swapchain->Height();
        target.format = desc_.swapchain->ImageFormat();
        target.initial = rhi::ResourceState::Undefined;
        target.final_state = rhi::ResourceState::Present;
    } else {
        target = *offscreen;
    }

    arena_.Reset();
    RenderGraph graph(&arena_, desc_.graph);
    if (!graph.Valid()) {
        SetError("the frame arena (%llu bytes) is too small for the graph's tables", static_cast<unsigned long long>(desc_.arena_bytes));
        return Abandon(FrameStatus::Failed, acquired);
    }
    FrameContext context;
    context.graph = &graph;
    context.target = graph.ImportTexture("target", target.texture, target.initial, target.final_state, false);
    context.width = target.width;
    context.height = target.height;
    context.format = target.format;
    context.scene = scene;
    context.camera = camera;
    context.device = device;
    context.frame_index = frame_;
    if (!desc_.build(context, desc_.build_user)) {
        SetError("the frame callback abandoned the frame");
        return Abandon(FrameStatus::Failed, acquired);
    }
    if (!graph.Compile()) {
        SetError("the graph did not compile: %s", graph.ErrorCount() > 0 ? graph.Error(0) : "(no message)");
        return Abandon(FrameStatus::Failed, acquired);
    }
    rhi::FenceValue fence = 0;
    if (!graph.ExecuteParallel(&pool_, device, desc_.scheduler, &fence)) {
        SetError("%s", graph.ExecuteError());
        return Abandon(device->DeviceLost() ? FrameStatus::DeviceLost : FrameStatus::Failed, acquired);
    }
    stats_.last_fence = fence;
    pool_.EndFrame();
    device->EndFrame();
    FrameStatus result = FrameStatus::Ok;
    if (acquired) {
        const rhi::SwapchainStatus presented = desc_.swapchain->Present();
        if (presented == rhi::SwapchainStatus::Lost) result = FrameStatus::DeviceLost;
        else if (presented == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
    }
    if (device->DeviceLost()) result = FrameStatus::DeviceLost;
    if (result == FrameStatus::Ok) ++stats_.frames_rendered;
    else ++stats_.frames_lost;
    return result;
}

} // namespace engine_placeholder::renderer
