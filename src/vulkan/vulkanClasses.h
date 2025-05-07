#pragma once
#include <deque>
#include <EOS.h>
#include <future>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "vkTools.h"
#include "pool.h"


//Forward Declares
struct VulkanShaderModuleState;
struct VulkanImage;
class VulkanContext;

static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

using VulkanShaderModulePool = EOS::Pool<EOS::ShaderModule, VulkanShaderModuleState>;
using VulkanTexturePool = EOS::Pool<EOS::Texture, VulkanImage>;

//TODO: split up in hot and cold data for the pool
struct VulkanShaderModuleState final
{
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    uint32_t PushConstantsSize = 0;
};

struct ImageDescription final
{
    VkImage Image{};
    VkImageUsageFlags UsageFlags{};
    VkExtent3D Extent{};
    EOS::ImageType ImageType{EOS::ImageType::Image_2D};
    VkFormat ImageFormat{};
    uint32_t Levels = 1;
    uint32_t Layers = 1;
    const char* DebugName{};
    VkDevice Device{};
};

//TODO: split up in hot and cold data for the pool
struct VulkanImage final
{
public:

    explicit VulkanImage(const ImageDescription& description);
    VulkanImage() = default;
    DELETE_COPY(VulkanImage);

    static constexpr uint32_t MaxMipLevels = 6;

    VulkanImage (VulkanImage&& other) noexcept = default;
    VulkanImage& operator=(VulkanImage&& other) noexcept = default;

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
    [[nodiscard]] const VkSurfaceFormatKHR& GetFormat() const;
    [[nodiscard]] EOS::TextureHandle GetCurrentTexture();

private:
    static constexpr uint32_t MAX_IMAGES{16};

    void GetAndWaitOnNextImage();
    [[nodiscard]] static VkSurfaceFormatKHR GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace);

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
    CommandBufferData() = default;
    DELETE_COPY_MOVE(CommandBufferData);

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
    ~CommandPool();
    DELETE_COPY_MOVE(CommandPool);

    void WaitSemaphore(const VkSemaphore& semaphore);
    void WaitAll();
    void Wait(const EOS::SubmitHandle handle);
    void Signal(const VkSemaphore& semaphore,const uint64_t& signalValue);
    [[nodiscard]] bool IsReady(EOS::SubmitHandle handle, bool fastCheck = false) const;

    VkSemaphore AcquireLastSubmitSemaphore();

    [[nodiscard]] EOS::SubmitHandle Submit(CommandBufferData& data);
    [[nodiscard]] EOS::SubmitHandle GetNextSubmitHandle() const;

private:
    // returns the current command buffer (creates one if it does not exist)
    CommandBufferData* AcquireCommandBuffer();

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
    CommandBufferData* CommandBufferImpl;
    VulkanContext* VkContext = nullptr;
};

struct DeferredTask
{
    DeferredTask(std::packaged_task<void()>&& task, EOS::SubmitHandle handle) : Task(std::move(task)), Handle(handle) {}
    DELETE_COPY_MOVE(DeferredTask);

    std::packaged_task<void()> Task;
    EOS::SubmitHandle Handle;
};

class VulkanContext final : public EOS::IContext
{
public:
    explicit VulkanContext(const EOS::ContextCreationDescription& contextDescription);
    ~VulkanContext() override;
    DELETE_COPY_MOVE(VulkanContext)

    [[nodiscard]] EOS::ICommandBuffer& AcquireCommandBuffer() override;
    [[nodiscard]] EOS::SubmitHandle Submit(EOS::ICommandBuffer &commandBuffer, EOS::TextureHandle present) override;
    [[nodiscard]] EOS::TextureHandle GetSwapChainTexture() override;
    void Destroy(EOS::TextureHandle handle) override;
    void Destroy(EOS::ShaderModuleHandle handle) override;

    void ProcessDeferredTasks() const;
    void Defer(std::packaged_task<void()>&& task, EOS::SubmitHandle handle = {}) const;


    std::unique_ptr<CommandPool> VulkanCommandPool = nullptr;
    VulkanShaderModulePool ShaderModulePool{};
    VulkanTexturePool TexturePool{};
private:
    [[nodiscard]] bool HasSwapChain() const noexcept;
    void CreateVulkanInstance(const char* applicationName);
    void SetupDebugMessenger();
    void CreateSurface(void* window, void* display);
    void GetHardwareDevice(EOS::HardwareDeviceType desiredDeviceType, std::vector<EOS::HardwareDeviceDescription>& compatibleDevices) const;
    void WaitOnDeferredTasks();
    [[nodiscard]] bool IsHostVisibleMemorySingleHeap() const;

private:
    VkInstance VulkanInstance                       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT VulkanDebugMessenger   = VK_NULL_HANDLE;
    VkPhysicalDevice VulkanPhysicalDevice           = VK_NULL_HANDLE;
    VkDevice VulkanDevice                           = VK_NULL_HANDLE;
    VkSurfaceKHR VulkanSurface                      = VK_NULL_HANDLE;
    VkSemaphore TimelineSemaphore                   = VK_NULL_HANDLE;
    std::unique_ptr<VulkanSwapChain> SwapChain      = nullptr;
    mutable std::deque<DeferredTask> DeferredTasks;

    CommandBuffer CurrentCommandBuffer;         //TODO: This needs to become a map or vector for multithreaded recording.
    DeviceQueues VulkanDeviceQueues{};
    EOS::ContextConfiguration Configuration{}; //TODO: Should the lifetime of this obj be the whole application?

    friend struct VulkanSwapChain;
    friend struct VulkanSwapChainSupportDetails;
};