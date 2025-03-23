#pragma once
#include <EOS.h>
#include <vector>
#include <volk.h>

namespace EOS
{
    //Forward Declares
    class VulkanContext;

    static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

    struct VulkanSwapChainCreationDescription final
    {
        VulkanContext& vulkanContext;
        uint32_t width{};
        uint32_t height{};
    };

    struct VulkanSwapChain final
    {
    private:
        static constexpr uint8_t MAX_IMAGES{16};

    public:
        VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription);
        ~VulkanSwapChain() = default;
        DELETE_COPY_MOVE(VulkanSwapChain)

    private:
        VkSurfaceFormatKHR SurfaceFormat = {.format = VK_FORMAT_UNDEFINED};

    };

    struct DeviceQueues final
    {
    constexpr static uint32_t InvalidIndex = 0xFFFFFFFF;

    private:
        struct DeviceQueueIndex final
        {
            uint32_t QueueFamilyIndex    = InvalidIndex;
            VkQueue Queue                = VK_NULL_HANDLE;
        };

    public:
        DeviceQueues() = default;
        ~DeviceQueues() = default;
        DELETE_COPY_MOVE(DeviceQueues)

        DeviceQueueIndex Graphics{};
        DeviceQueueIndex Compute{};
    };

    class VulkanContext final : public IContext
    {
    public:
        explicit VulkanContext(const ContextCreationDescription& contextDescription);
        ~VulkanContext() override = default;
        DELETE_COPY_MOVE(VulkanContext)


    private:
        void CreateVulkanInstance();
        void SetupDebugMessenger();
        void CreateSurface(void* window, void* display);
        void GetHardwareDevice(HardwareDeviceType desiredDeviceType, std::vector<HardwareDeviceDescription>& compatibleDevices) const;
        [[nodiscard]] bool IsHostVisibleMemorySingleHeap() const;

    private:
        VkInstance VulkanInstance                       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT VulkanDebugMessenger   = VK_NULL_HANDLE;
        VkPhysicalDevice VulkanPhysicalDevice           = VK_NULL_HANDLE;
        VkDevice VulkanDevice                           = VK_NULL_HANDLE;
        VkSurfaceKHR VulkanSurface                      = VK_NULL_HANDLE;
        EOS::DeviceQueues VulkanDeviceQueues{};
        ContextConfiguration Configuration{};
    };
}
