#pragma once
#include <cstdio>

#include <volk.h>
#include <vk_mem_alloc.h>



#include "cassert"

#define VK_ASSERT(func){ const VkResult result = func; if (result != VK_SUCCESS) { printf("Vulkan Assert failed: %s:%i\n", __FILE__, __LINE__); assert(false); } }

// Forward Declare
namespace EOS
{
    struct HardwareDeviceDescription;
    enum class ColorSpace : uint8_t;
}

VKAPI_ATTR inline VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
    //TODO: Have logging based or severity
    printf("%s\n", callbackData->pMessage);
    return VK_TRUE;
}

[[nodiscard]] VkResult SetDebugObjectName(const VkDevice device, const VkObjectType type, const uint64_t handle, const char* name);
[[nodiscard]] uint32_t FindQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags);

void CheckMissingDeviceFeatures(
    const VkPhysicalDeviceFeatures& deviceFeatures10, const VkPhysicalDeviceFeatures2& vkFeatures10,
    const VkPhysicalDeviceVulkan11Features& deviceFeatures11, const VkPhysicalDeviceVulkan11Features& vkFeatures11,
    const VkPhysicalDeviceVulkan12Features& deviceFeatures12, const VkPhysicalDeviceVulkan12Features& vkFeatures12,
    const VkPhysicalDeviceVulkan13Features& deviceFeatures13, const VkPhysicalDeviceVulkan13Features& vkFeatures13);

void SelectHardwareDevice(const std::vector<EOS::HardwareDeviceDescription>& hardwareDevices, VkPhysicalDevice& physicalDevice);
void GetDeviceExtensions(std::vector<VkExtensionProperties>& deviceExtensions,const VkPhysicalDevice& vulkanPhysicalDevice, const char* forValidationLayer = nullptr);
void PrintDeviceExtensions(const VkPhysicalDevice& vulkanPhysicalDevice, std::vector<VkExtensionProperties>& allDeviceExtensions);
void GetPhysicalDeviceProperties(VkPhysicalDeviceProperties2& physicalDeviceProperties, VkPhysicalDeviceDriverProperties& physicalDeviceDriverProperties, VkPhysicalDevice physicalDevice);
[[nodiscard]] VkSurfaceFormatKHR GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace);
[[nodiscard]] VkSemaphore CreateSemaphore(VkDevice device, const char* debugName);
