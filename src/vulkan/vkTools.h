#pragma once
#include <volk.h>
#include "cassert"
#define VK_ASSERT(func){ const VkResult result = func; if (result != VK_SUCCESS) { printf("Vulkan Assert failed: %s:%i\n", __FILE__, __LINE__); assert(false); } }

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
    printf(callbackData->pMessage);
    return VK_TRUE;
}