#include "rhi/vulkan/vk_barrier.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

constexpr VkPipelineStageFlags2 kShaderStages =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

struct Row {
    StateScope scope;
    bool buffer_ok;
    bool image_ok;
};

// Indexed by ResourceState. Every state's stages, accesses and layout live here and nowhere else.
const Row* TableRow(ResourceState state) {
    static const Row kTable[] = {
        // Undefined: wait for everything earlier; as a source this is the conservative scope.
        {{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}, true, true},
        // VertexBuffer
        {{VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}, true, false},
        // IndexBuffer
        {{VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}, true, false},
        // UniformBuffer
        {{kShaderStages, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}, true, false},
        // ShaderRead
        {{kShaderStages, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, true, true},
        // ShaderWrite
        {{kShaderStages, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL}, true, true},
        // CopySrc
        {{VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, true, true},
        // CopyDst
        {{VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}, true, true},
        // ColorAttachment
        {{VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, false, true},
        // DepthAttachment
        {{kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}, false, true},
        // DepthRead
        {{kDepthStages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}, false, true},
        // Present: nothing to wait for on the GPU side; the presentation engine is synchronized by a semaphore.
        {{VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}, false, true},
    };
    static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<usize>(ResourceState::Count), "one row per ResourceState");
    const usize index = static_cast<usize>(state);
    return index < static_cast<usize>(ResourceState::Count) ? &kTable[index] : nullptr;
}

} // namespace

bool GetBarrierScopes(ResourceState before, ResourceState after, bool is_image, BarrierScopes* out) {
    const Row* src = TableRow(before);
    const Row* dst = TableRow(after);
    if (src == nullptr || dst == nullptr || after == ResourceState::Undefined) {
        return false;
    }
    if (is_image ? (!src->image_ok || !dst->image_ok) : (!src->buffer_ok || !dst->buffer_ok)) {
        return false;
    }
    out->src = src->scope;
    out->dst = dst->scope;
    if (!is_image) {
        out->src.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        out->dst.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return true;
}

} // namespace engine_placeholder::rhi::vulkan
