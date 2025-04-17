#pragma once
#include <EOS.h>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "vkTools.h"
#include "pool.h"

//Forward Declares
struct VulkanImage;
class VulkanContext;

using VulkanTexturePool = EOS::Pool<EOS::Texture, VulkanImage>;

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
    VulkanImage() = default;

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

struct VulkanSwapChainSupportDetails final
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
    [[nodiscard]] VkSurfaceFormatKHR GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace);

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

    friend class VulkanContext;
};

struct CommandBufferData
{
    VkCommandBuffer VulkanCommandBuffer             = VK_NULL_HANDLE;
    VkCommandBuffer VulkanCommandBufferAllocated    = VK_NULL_HANDLE;
    VkFence Fence                                   = VK_NULL_HANDLE;
    VkSemaphore Semaphore                           = VK_NULL_HANDLE;
    EOS::SubmitHandle Handle                        = {};
    bool isEncoding                                 = false;
};

//TODO: Command Recording should be done on multiple threads
// Each thread having its own pool
class CommandPool final
{
    static constexpr uint32_t MaxCommandBuffers = 64;
public:
    explicit CommandPool(const VkDevice& device, uint32_t queueIndex);
    ~CommandPool() = default;
    DELETE_COPY_MOVE(CommandPool);

    void WaitSemaphore(const VkSemaphore& semaphore);
    void Signal(const VkSemaphore& semaphore,const uint64_t& signalValue);

    VkSemaphore AcquireLastSubmitSemaphore();

    EOS::SubmitHandle Submit(CommandBufferData& data);

private:
    // returns the current command buffer (creates one if it does not exist)
    const CommandBufferData& AcquireCommandBuffer();

    //This will go over all command buffers and try to restore them to their initial state when they are not in use.
    void TryResetCommandBuffers();

private:
    std::array<CommandBufferData, MaxCommandBuffers> Buffers;
    uint32_t NumberOfAvailableCommandBuffers{};

    uint32_t SubmitCounter = 1;
    EOS::SubmitHandle NextSubmitHandle{};
    EOS::SubmitHandle LastSubmitHandle{};

    std::vector<VkSemaphoreSubmitInfo> WaitSemaphores{};
    std::vector<VkSemaphoreSubmitInfo> SignalSemaphores{};

    VkSemaphoreSubmitInfo WaitOnSemaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    VkSemaphoreSubmitInfo LastSubmitSemaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    VkSemaphoreSubmitInfo SignalSemaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    VkCommandPool VulkanCommandPool = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    friend class CommandBuffer;
};

class CommandBuffer final : public EOS::ICommandBuffer
{
public:
    CommandBuffer() = default;
    explicit CommandBuffer(VulkanContext* vulkanContext);;

    ~CommandBuffer() override = default;
    DELETE_COPY(CommandBuffer)

    CommandBuffer (CommandBuffer&&) = delete;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;

    explicit operator bool() const;

    EOS::SubmitHandle LastSubmitHandle{};
    CommandBufferData CommandBufferImpl{};
    VulkanContext* VkContext = nullptr;
};

class VulkanContext final : public EOS::IContext
{
public:
    explicit VulkanContext(const EOS::ContextCreationDescription& contextDescription);
    ~VulkanContext() override = default;
    DELETE_COPY_MOVE(VulkanContext)

    [[nodiscard]] EOS::ICommandBuffer& AcquireCommandBuffer() override;
    [[nodiscard]] EOS::SubmitHandle Submit(EOS::ICommandBuffer &commandBuffer, EOS::TextureHandle present) override;
    [[nodiscard]] EOS::TextureHandle GetSwapChainTexture() override;
    [[nodiscard]] const CommandBuffer* GetCurrentCommandBuffer() const;


    std::unique_ptr<CommandPool> VulkanCommandPool = nullptr;
    VulkanTexturePool TexturePool{};
private:
    [[nodiscard]] bool HasSwapChain() const noexcept;
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

    CommandBuffer CurrentCommandBuffer{};

    DeviceQueues VulkanDeviceQueues{};
    EOS::ContextConfiguration Configuration{}; //TODO: Should the liftime of this obj be the whole application?

    friend struct VulkanSwapChain;
    friend struct VulkanSwapChainSupportDetails;
};

