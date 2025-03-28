#pragma once
#include <EOS.h>
#include <vector>

#include "vkTools.h"

#include <volk.h>
#include <vk_mem_alloc.h>

//Forward Declares
class VulkanContext;
static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

struct ImageDescription final
{
    VkImage Image{};
    VkImageUsageFlags UsageFlags{};
    VkExtent3D Extent{};
    EOS::ImageType ImageType;
    VkFormat ImageFormat{};
    const char* DebugName{};
    VkDevice Device{};
};

struct VulkanImage final
{
public:
    explicit VulkanImage(const ImageDescription& description);
    //TODO:
    //static bool IsDepth()
    //static bool IsStencil()
    static void CreateImageView(VkImageView& imageView, VkDevice device, VkImage image, EOS::ImageType imageType, const VkFormat& imageFormat, uint32_t levels, uint32_t layers ,const char* debugName);
    static VkImageType ToImageType(EOS::ImageType imageType);
    static VkImageViewType ToImageViewType(EOS::ImageType imageType);

public:
    VkImage Image                           = VK_NULL_HANDLE;
    VkImageUsageFlags UsageFlags            = 0;
    VmaAllocation Allocation                = VK_NULL_HANDLE;
    VkFormatProperties FormatProperties     = {};
    VkExtent3D Extent                       = {0, 0, 0};
    EOS::ImageType ImageType                = EOS::ImageType::Image_2D;
    VkFormat ImageFormat                    = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits Samples           = VK_SAMPLE_COUNT_1_BIT;
    void* MappedPtr                         = nullptr;
    bool IsOwningImage                      = true;
    uint32_t Levels                         = 1;
    uint32_t Layers                         = 1;
    mutable VkImageLayout ImageLayout       = VK_IMAGE_LAYOUT_UNDEFINED;


    // precached image views - owned by this VulkanImage
    VkImageView ImageView                                   = VK_NULL_HANDLE;       // default view with all mip-levels
    VkImageView ImageViewStorage                            = VK_NULL_HANDLE;       // default view with identity swizzle (all mip-levels)
    VkImageView ImageViewForFramebuffer[MAX_MIP_LEVELS][6]  = {};                   // max 6 faces for cubemap rendering
};

struct VulkanSwapChainCreationDescription final
{
    VulkanContext* vulkanContext{};
    uint32_t width{};
    uint32_t height{};
};

struct VulkanSwapChain final
{
private:
    static constexpr uint32_t MAX_IMAGES{16};

public:
    explicit VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription);
    ~VulkanSwapChain() = default;
    DELETE_COPY_MOVE(VulkanSwapChain)

private:
    struct VulkanSwapChainSupportDetails
    {
        explicit VulkanSwapChainSupportDetails(const VulkanContext& vulkanContext);

        VkSurfaceCapabilitiesKHR capabilities{};
        VkBool32 queueFamilySupportsPresentation{ VK_FALSE };
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;

    };

    VkSurfaceFormatKHR SurfaceFormat = {.format = VK_FORMAT_UNDEFINED};
    VkSwapchainKHR SwapChain{ VK_NULL_HANDLE };
    uint32_t NumberOfSwapChainImages{};

    VkSemaphore AcquireSemaphore [MAX_IMAGES]{};
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

class VulkanContext final : public EOS::IContext
{
public:
    explicit VulkanContext(const EOS::ContextCreationDescription& contextDescription);
    ~VulkanContext() override = default;
    DELETE_COPY_MOVE(VulkanContext)


private:
    //TODO: All of these private functions should be moved to VkTools
    //The Context Object will "only" have functions that are being used at runtime / not initial setup.
    void CreateVulkanInstance();
    void SetupDebugMessenger();
    void CreateSurface(void* window, void* display);
    void GetHardwareDevice(EOS::HardwareDeviceType desiredDeviceType, std::vector<EOS::HardwareDeviceDescription>& compatibleDevices) const;
    [[nodiscard]] bool IsHostVisibleMemorySingleHeap() const;

private:
    VkInstance VulkanInstance                       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT VulkanDebugMessenger   = VK_NULL_HANDLE;
    VkPhysicalDevice VulkanPhysicalDevice           = VK_NULL_HANDLE;
    VkDevice VulkanDevice                           = VK_NULL_HANDLE;
    VkSurfaceKHR VulkanSurface                      = VK_NULL_HANDLE;
    DeviceQueues VulkanDeviceQueues{};
    EOS::ContextConfiguration Configuration{};

    friend struct VulkanSwapChain;
};

