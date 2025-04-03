#pragma once
#include <cstdio>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "logger.h"

#define VK_ASSERT(func){ const VkResult result = func;      \
    CHECK(result == VK_SUCCESS, "Vulkan Assert failed: ");   \
}

#pragma region ForwardDeclare
struct DeviceQueues;
namespace EOS
{
    struct HardwareDeviceDescription;
    enum class ColorSpace : uint8_t;
}
#pragma endregion

namespace VkDebug
{
    [[nodiscard]] const char* ObjectToString(VkObjectType objectType);
    [[nodiscard]] VkResult SetDebugObjectName(const VkDevice& device, const VkObjectType& type, const uint64_t handle, const char* name);
    VKAPI_ATTR inline VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
    {
        //Move all VOLK messages to the verbose bit
        if (strcmp(callbackData->pMessageIdName, "Loader Message") == 0  && messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
        }

        // Format message with additional context
        std::string message = fmt::format("[{}] {}", callbackData->pMessageIdName ? callbackData->pMessageIdName : "unknown", callbackData->pMessage);

        if (callbackData->objectCount > 0)
        {
            auto formatObjectInfo = [](const VkDebugUtilsObjectNameInfoEXT& obj)
            {
                const auto& [sType, pNext, objectType, objectHandle, pObjectName] = obj;
                return pObjectName ? fmt::format("{} (Type: {}, Handle: {:#x})", pObjectName, ObjectToString(objectType), reinterpret_cast<uint64_t>(objectHandle)) : fmt::format("Type: {}, Handle: {:#x}", ObjectToString(objectType), reinterpret_cast<uint64_t>(objectHandle));
            };

            std::vector<std::string> objectEntries;
            objectEntries.reserve(callbackData->objectCount);

            for (uint32_t i{}; i < callbackData->objectCount; ++i)
            {
                objectEntries.emplace_back(formatObjectInfo(callbackData->pObjects[i]));
            }

            message += fmt::format("\n\tAssociated Objects [{}]: {}", callbackData->objectCount, fmt::join(objectEntries, " | "));
        }

        // Determine if we should abort execution
        VkBool32 shouldAbort = VK_FALSE;

        // Check severities in descending order of importance
        if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            EOS::Logger->error(message);
            shouldAbort = VK_TRUE;  // Abort on errors
        }
        else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            EOS::Logger->warn(message);
        }
        else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            EOS::Logger->info(message);
        }
        else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        {
            EOS::Logger->debug(message);
        }
        else
        {
            EOS::Logger->warn("Unknown severity message: {}", message);
        }

        return shouldAbort;
    }
}

//TODO: Move them back to VulkanContext but make them static and private
namespace VkContext
{
    [[nodiscard]] uint32_t FindQueueFamilyIndex(const VkPhysicalDevice& physicalDevice, VkQueueFlags flags);

    void CheckMissingDeviceFeatures(
    const VkPhysicalDeviceFeatures& deviceFeatures10, const VkPhysicalDeviceFeatures2& vkFeatures10,
    const VkPhysicalDeviceVulkan11Features& deviceFeatures11, const VkPhysicalDeviceVulkan11Features& vkFeatures11,
    const VkPhysicalDeviceVulkan12Features& deviceFeatures12, const VkPhysicalDeviceVulkan12Features& vkFeatures12,
    const VkPhysicalDeviceVulkan13Features& deviceFeatures13, const VkPhysicalDeviceVulkan13Features& vkFeatures13,
    const std::optional<VkPhysicalDeviceVulkan14Features>& deviceFeatures14 = std::nullopt, const std::optional<VkPhysicalDeviceVulkan14Features>& vkFeatures14= std::nullopt);

    void SelectHardwareDevice(const std::vector<EOS::HardwareDeviceDescription>& hardwareDevices, VkPhysicalDevice& physicalDevice);
    void GetDeviceExtensions(std::vector<VkExtensionProperties>& deviceExtensions,const VkPhysicalDevice& vulkanPhysicalDevice, const char* forValidationLayer = nullptr);
    void GetDeviceExtensions(const VkPhysicalDevice& vulkanPhysicalDevice, std::vector<VkExtensionProperties>& allDeviceExtensions);
    void GetPhysicalDeviceProperties(VkPhysicalDeviceProperties2& physicalDeviceProperties, VkPhysicalDeviceDriverProperties& physicalDeviceDriverProperties, VkPhysicalDevice physicalDevice, uint32_t SDKMinorVersion);
    void CreateVulkanDevice(VkDevice& device, const VkPhysicalDevice& physicalDevice, DeviceQueues& deviceQueues);
}

//TODO: Move them back to VulkanSwapChain but make them static and private
namespace VkSwapChain
{
    [[nodiscard]] VkSurfaceFormatKHR GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace);
}

namespace VkSynchronization
{
    [[nodiscard]] VkSemaphore CreateSemaphore(const VkDevice& device, const char* debugName);
    [[nodiscard]] VkSemaphore CreateSemaphoreTimeline(const VkDevice& device, uint64_t initialValue, const char* debugName);

}