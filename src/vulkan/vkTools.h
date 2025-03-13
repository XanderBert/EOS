#pragma once
#include <volk.h>
#include "cassert"
#define VK_ASSERT(func){ const VkResult result = func; if (result != VK_SUCCESS) { printf("Vulkan Assert failed: %s:%i\n", __FILE__, __LINE__); assert(false); } }