#pragma once
#include <EOS.h>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "vkTools.h"
#include "pool.h"

//Forward Declares
class VulkanContext;
static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

struct ImageDescription final
{
    VkImage Image{};
    VkImageUsageFlags UsageFlags{};
    VkExtent3D Extent{};
    EOS::ImageType ImageType{EOS::ImageType::Image_2D};
    VkFormat ImageFormat{};
    const char* DebugName{};
    VkDevice Device{};
};

//TODO: split up in hot and cold data for the pool
struct VulkanImage final
{
private:
    static constexpr uint32_t MaxMipLevels = 6;
public:

    explicit VulkanImage(const ImageDescription& description);

    [[nodiscard]] static inline bool IsSampledImage(const VulkanImage& image) { return (image.UsageFlags & VK_IMAGE_USAGE_SAMPLED_BIT) > 0; }
    [[nodiscard]] static inline bool IsStorageImage(const VulkanImage& image) { return (image.UsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) > 0; }
    [[nodiscard]] static inline bool IsColorAttachment(const VulkanImage& image) { return (image.UsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) > 0; }
    [[nodiscard]] static inline bool IsDepthAttachment(const VulkanImage& image) { return (image.UsageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) > 0; }
    [[nodiscard]] static inline bool IsAttachment(const VulkanImage& image) { return (image.UsageFlags & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) > 0; }
    [[nodiscard]] static inline bool IsSwapChainImage(const VulkanImage& image) { return (image.ImageType == EOS::ImageType::SwapChain); }
    [[nodiscard]] static VkImageType ToImageType(EOS::ImageType imageType);
    [[nodiscard]] static VkImageViewType ToImageViewType(EOS::ImageType imageType);

    static void CreateImageView(VkImageView& imageView, VkDevice device, VkImage image, EOS::ImageType imageType, const VkFormat& imageFormat, uint32_t levels, uint32_t layers ,const char* debugName);
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
    VkImageView ImageViewForFramebuffer[MaxMipLevels][6]  = {};                   // max 6 faces for cubemap rendering
};

struct VulkanSwapChainCreationDescription final
{
    VulkanContext* vulkanContext{};
    uint32_t width{};
    uint32_t height{};
};

struct VulkanSwapChainSupportDetails
{
    explicit VulkanSwapChainSupportDetails(const VulkanContext& vulkanContext);

    VkSurfaceCapabilitiesKHR capabilities{};
    VkBool32 queueFamilySupportsPresentation{ VK_FALSE };
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct DeviceQueueIndex final
{
    constexpr static uint32_t InvalidIndex = 0xFFFFFFFF;

    DeviceQueueIndex() = default;
    ~DeviceQueueIndex() = default;
    DELETE_COPY_MOVE(DeviceQueueIndex);

    uint32_t QueueFamilyIndex    = InvalidIndex;
    VkQueue Queue                = VK_NULL_HANDLE;
};

struct DeviceQueues final
{
    DeviceQueues() = default;
    ~DeviceQueues() = default;
    DELETE_COPY_MOVE(DeviceQueues)

    DeviceQueueIndex Graphics{};
    DeviceQueueIndex Compute{};
};

struct VulkanSwapChain final
{
public:
    explicit VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription);
    ~VulkanSwapChain();
    DELETE_COPY_MOVE(VulkanSwapChain)

    void Present(VkSemaphore waitSemaphore);

    [[nodiscard]] VkImage GetCurrentImage() const;
    [[nodiscard]] VkImageView GetCurrentImageView() const;
    [[nodiscard]] uint32_t GetNumSwapChainImages() const;
    [[nodiscard]] const VkSurfaceFormatKHR& GetFormat() const;  //Return by const-ref because VkSurfaceFormatKHR is not a handle
    [[nodiscard]] EOS::TextureHandle GetCurrentTexture();       //Also Gets the Next Image when needed

private:
    static constexpr uint32_t MAX_IMAGES{16};

    void GetAndWaitOnNextImage();

    VkSurfaceFormatKHR SurfaceFormat = {.format = VK_FORMAT_UNDEFINED};
    VkSwapchainKHR SwapChain{ VK_NULL_HANDLE };
    uint32_t NumberOfSwapChainImages{};
    uint32_t CurrentImageIndex{};
    uint64_t CurrentFrame{};
    bool GetNextImage{true};

    std::vector<VkSemaphore> AcquireSemaphores{};
    std::vector<EOS::TextureHandle> Textures{};
    std::vector<uint64_t> TimelineWaitValues{};

    VulkanContext* VkContext = nullptr;
    VkQueue GraphicsQueue{};
};

class VulkanCommands final
{
    static constexpr uint32_t MaxCommandBuffers = 64;
public:
    void WaitSemaphore(const VkSemaphore& semaphore);

private:
    VkSemaphoreSubmitInfo WaitOnSemaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
};

class VulkanContext final : public EOS::IContext
{
public:
    explicit VulkanContext(const EOS::ContextCreationDescription& contextDescription);
    ~VulkanContext() override = default;
    DELETE_COPY_MOVE(VulkanContext)

    std::unique_ptr<VulkanCommands> Commands = nullptr;

    EOS::Pool<EOS::Texture, VulkanImage> TexturePool{};
private:
    //TODO: All of these private functions should be made static
    //The Context Object will "only" have  non-static functions that are being used at runtime / not initial setup.
    void CreateVulkanInstance(const char* applicationName);
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
    VkSemaphore TimelineSemaphore                   = VK_NULL_HANDLE;
    std::unique_ptr<VulkanSwapChain> SwapChain      = nullptr;

    DeviceQueues VulkanDeviceQueues{};
    EOS::ContextConfiguration Configuration{};

    friend struct VulkanSwapChain;
    friend struct VulkanSwapChainSupportDetails;
};

