#pragma once
#include <EOS.h>
#include <vector>
#include <volk.h>

namespace EOS
{
    static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

    class VulkanContext final : public IContext
    {
    public:
        VulkanContext(const ContextCreationDescription& contextDescription);
        ~VulkanContext() override = default;
        DELETE_COPY_MOVE(VulkanContext)


    private:
        void CreateVulkanInstance();
        void SetupDebugMessenger();
        void CreateSurface(void* window, void* display);
        void GetHardwareDevice(HardwareDeviceType desiredDeviceType, std::vector<HardwareDeviceDescription>& compatibleDevices) const;
        [[nodiscard]] bool IsHostVisibleMemorySingleHeap() const;
        void GetDeviceExtensions(std::vector<VkExtensionProperties> deviceExtensions, const char* forValidationLayer = nullptr) const;

    private:
        VkInstance VulkanInstance                       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT VulkanDebugMessenger   = VK_NULL_HANDLE;
        VkPhysicalDevice VulkanPhysicalDevice           = VK_NULL_HANDLE;
        VkSurfaceKHR VulkanSurface                      = VK_NULL_HANDLE;
        ContextConfiguration Configuration;
    };
}
