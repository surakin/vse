// Win32 virtual keys are allowed here, Vulkan is not.
int Key(unsigned vk) { return vk == VK_ESCAPE ? 1 : 0; }
void Leak() { vkCreateInstance(nullptr, nullptr, nullptr); }
