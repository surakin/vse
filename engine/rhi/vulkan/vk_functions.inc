// X-list of every Vulkan entry point the backend resolves at runtime. Adding a
// function is one line here: the table member, the name string and the load
// code are all generated from it.
//
// Including files define the six macros below (unused ones may be left
// undefined); they are undefined again at the end of this file.
//   VK_GLOBAL_FN / VK_GLOBAL_FN_OPT     resolved with a null instance
//   VK_INSTANCE_FN / VK_INSTANCE_FN_OPT resolved against a VkInstance
//   VK_DEVICE_FN / VK_DEVICE_FN_OPT     resolved against a VkDevice
// `_OPT` entries may be absent (null) after loading; the others are mandatory
// and a missing one fails the load, naming it.

#ifndef VK_GLOBAL_FN
#define VK_GLOBAL_FN(name)
#endif
#ifndef VK_GLOBAL_FN_OPT
#define VK_GLOBAL_FN_OPT(name)
#endif
#ifndef VK_INSTANCE_FN
#define VK_INSTANCE_FN(name)
#endif
#ifndef VK_INSTANCE_FN_OPT
#define VK_INSTANCE_FN_OPT(name)
#endif
#ifndef VK_DEVICE_FN
#define VK_DEVICE_FN(name)
#endif
#ifndef VK_DEVICE_FN_OPT
#define VK_DEVICE_FN_OPT(name)
#endif

// ---- Global (no instance) ----
VK_GLOBAL_FN(CreateInstance)
VK_GLOBAL_FN(EnumerateInstanceExtensionProperties)
VK_GLOBAL_FN(EnumerateInstanceLayerProperties)
VK_GLOBAL_FN_OPT(EnumerateInstanceVersion) // absent on a Vulkan 1.0 loader, which is then reported "too old"

// ---- Instance ----
VK_INSTANCE_FN(DestroyInstance)
VK_INSTANCE_FN(EnumeratePhysicalDevices)
VK_INSTANCE_FN(GetPhysicalDeviceProperties)
VK_INSTANCE_FN(GetPhysicalDeviceProperties2)
VK_INSTANCE_FN(GetPhysicalDeviceFeatures2)
VK_INSTANCE_FN(GetPhysicalDeviceMemoryProperties)
VK_INSTANCE_FN(GetPhysicalDeviceMemoryProperties2)
VK_INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties)
VK_INSTANCE_FN(GetPhysicalDeviceFormatProperties)
VK_INSTANCE_FN(EnumerateDeviceExtensionProperties)
VK_INSTANCE_FN(CreateDevice)
VK_INSTANCE_FN(GetDeviceProcAddr)
// Presentation surfaces (VK_KHR_surface and the per-platform extensions); absent when the instance
// was created without them, e.g. for headless tests.
VK_INSTANCE_FN_OPT(DestroySurfaceKHR)
VK_INSTANCE_FN_OPT(GetPhysicalDeviceSurfaceSupportKHR)
VK_INSTANCE_FN_OPT(GetPhysicalDeviceSurfaceCapabilitiesKHR)
VK_INSTANCE_FN_OPT(GetPhysicalDeviceSurfaceFormatsKHR)
VK_INSTANCE_FN_OPT(GetPhysicalDeviceSurfacePresentModesKHR)
#ifdef VK_KHR_wayland_surface
VK_INSTANCE_FN_OPT(CreateWaylandSurfaceKHR)
#endif
// vkCreateXlibSurfaceKHR is added on Day 56 (see vk_loader.h for why it is not here yet).
// Validation output (VK_EXT_debug_utils), development builds only.
VK_INSTANCE_FN_OPT(CreateDebugUtilsMessengerEXT)
VK_INSTANCE_FN_OPT(DestroyDebugUtilsMessengerEXT)

// ---- Device ----
// Lifetime and queues
VK_DEVICE_FN(DestroyDevice)
VK_DEVICE_FN(GetDeviceQueue)
VK_DEVICE_FN(DeviceWaitIdle)
VK_DEVICE_FN(QueueWaitIdle)
VK_DEVICE_FN(QueueSubmit2)
// Memory
VK_DEVICE_FN(AllocateMemory)
VK_DEVICE_FN(FreeMemory)
VK_DEVICE_FN(MapMemory)
VK_DEVICE_FN(UnmapMemory)
VK_DEVICE_FN(FlushMappedMemoryRanges)
VK_DEVICE_FN(InvalidateMappedMemoryRanges)
// Buffers, images, samplers
VK_DEVICE_FN(CreateBuffer)
VK_DEVICE_FN(DestroyBuffer)
VK_DEVICE_FN(GetBufferMemoryRequirements)
VK_DEVICE_FN(BindBufferMemory)
VK_DEVICE_FN(CreateImage)
VK_DEVICE_FN(DestroyImage)
VK_DEVICE_FN(GetImageMemoryRequirements)
VK_DEVICE_FN(BindImageMemory)
VK_DEVICE_FN(CreateImageView)
VK_DEVICE_FN(DestroyImageView)
VK_DEVICE_FN(CreateSampler)
VK_DEVICE_FN(DestroySampler)
// Shaders and pipelines
VK_DEVICE_FN(CreateShaderModule)
VK_DEVICE_FN(DestroyShaderModule)
VK_DEVICE_FN(CreateDescriptorSetLayout)
VK_DEVICE_FN(DestroyDescriptorSetLayout)
VK_DEVICE_FN(CreatePipelineLayout)
VK_DEVICE_FN(DestroyPipelineLayout)
VK_DEVICE_FN(CreateGraphicsPipelines)
VK_DEVICE_FN(CreateComputePipelines)
VK_DEVICE_FN(DestroyPipeline)
// Descriptors
VK_DEVICE_FN(CreateDescriptorPool)
VK_DEVICE_FN(DestroyDescriptorPool)
VK_DEVICE_FN(ResetDescriptorPool)
VK_DEVICE_FN(AllocateDescriptorSets)
VK_DEVICE_FN(UpdateDescriptorSets)
// Command buffers
VK_DEVICE_FN(CreateCommandPool)
VK_DEVICE_FN(DestroyCommandPool)
VK_DEVICE_FN(ResetCommandPool)
VK_DEVICE_FN(AllocateCommandBuffers)
VK_DEVICE_FN(FreeCommandBuffers)
VK_DEVICE_FN(BeginCommandBuffer)
VK_DEVICE_FN(EndCommandBuffer)
VK_DEVICE_FN(CmdPipelineBarrier2)
VK_DEVICE_FN(CmdBeginRendering)
VK_DEVICE_FN(CmdEndRendering)
VK_DEVICE_FN(CmdBindPipeline)
VK_DEVICE_FN(CmdBindDescriptorSets)
VK_DEVICE_FN(CmdBindVertexBuffers)
VK_DEVICE_FN(CmdBindIndexBuffer)
VK_DEVICE_FN(CmdPushConstants)
VK_DEVICE_FN(CmdSetViewport)
VK_DEVICE_FN(CmdSetScissor)
VK_DEVICE_FN(CmdDraw)
VK_DEVICE_FN(CmdDrawIndexed)
VK_DEVICE_FN(CmdDispatch)
VK_DEVICE_FN(CmdCopyBuffer)
VK_DEVICE_FN(CmdCopyBufferToImage)
VK_DEVICE_FN(CmdCopyImageToBuffer)
// Synchronization (timeline semaphores are core in 1.2, binary ones are used for presentation)
VK_DEVICE_FN(CreateSemaphore)
VK_DEVICE_FN(DestroySemaphore)
VK_DEVICE_FN(WaitSemaphores)
VK_DEVICE_FN(GetSemaphoreCounterValue)
// Swapchain (absent when the device was created without VK_KHR_swapchain)
VK_DEVICE_FN_OPT(CreateSwapchainKHR)
VK_DEVICE_FN_OPT(DestroySwapchainKHR)
VK_DEVICE_FN_OPT(GetSwapchainImagesKHR)
VK_DEVICE_FN_OPT(AcquireNextImageKHR)
VK_DEVICE_FN_OPT(QueuePresentKHR)
// Debug names and labels (VK_EXT_debug_utils)
VK_DEVICE_FN_OPT(SetDebugUtilsObjectNameEXT)
VK_DEVICE_FN_OPT(CmdBeginDebugUtilsLabelEXT)
VK_DEVICE_FN_OPT(CmdEndDebugUtilsLabelEXT)

#undef VK_GLOBAL_FN
#undef VK_GLOBAL_FN_OPT
#undef VK_INSTANCE_FN
#undef VK_INSTANCE_FN_OPT
#undef VK_DEVICE_FN
#undef VK_DEVICE_FN_OPT
