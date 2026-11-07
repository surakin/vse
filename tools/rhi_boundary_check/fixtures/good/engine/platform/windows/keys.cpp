// The Win32 platform directory names virtual keys, whose names share Vulkan's VK_ prefix.
int Key(unsigned vk) { return vk == VK_ESCAPE || vk == VK_SHIFT || vk == VK_F12 ? 1 : 0; }
