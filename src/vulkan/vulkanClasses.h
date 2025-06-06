#pragma once
#include <deque>
#include <EOS.h>
#include <future>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "vkTools.h"
#include "pool.h"

struct VulkanRenderPipelineState;
struct VulkanShaderModuleState;
struct VulkanImage;
struct VulkanBuffer;
class VulkanContext;

static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

using VulkanRenderPipelinePool = EOS::Pool<EOS::RenderPipeline, VulkanRenderPipelineState>;
using VulkanShaderModulePool = EOS::Pool<EOS::ShaderModule, VulkanShaderModuleState>;
using VulkanTexturePool = EOS::Pool<EOS::Texture, VulkanImage>;
using VulkanBufferPool = EOS::Pool<EOS::Buffer, VulkanBuffer>;

//TODO: Split up in hot and cold data
struct VulkanBuffer final
{
    [[nodiscard]] inline uint8_t* GetMappedPtr() const { return static_cast<uint8_t*>(MappedPtr); }
    [[nodiscard]] inline bool IsMapped() const { return MappedPtr != nullptr;  }

    void BufferSubData(const VulkanContext* vulkanContext, size_t offset, size_t size, const void* data);
    void FlushMappedMemory(const VulkanContext* vulkanContext, VkDeviceSize offset, VkDeviceSize size) const;

    VkBuffer VulkanVkBuffer             = VK_NULL_HANDLE;
    VkDeviceMemory VulkanMemory         = VK_NULL_HANDLE;
    VmaAllocation VMAAllocation         = VK_NULL_HANDLE;
    VkDeviceAddress VulkanDeviceAddress = 0;
    VkDeviceSize BufferSize             = 0;
    VkBufferUsageFlags VkUsageFlags     = 0;
    VkMemoryPropertyFlags VkMemoryFlags = 0;
    void* MappedPtr                     = nullptr;
    bool IsCoherentMemory               = false;
};

//TODO: split up in hot and cold data for the pool
struct VulkanRenderPipelineState final
{
    EOS::RenderPipelineDescription Description;
    uint32_t NumberOfBindings = 0;
    uint32_t NumberOfAttributes = 0;

    VkVertexInputBindingDescription Bindings[EOS::VertexInputData::MAX_BUFFERS] = {};
    VkVertexInputAttributeDescription Attributes[EOS::VertexInputData::MAX_ATTRIBUTES] = {};

    // non-owning, the last seen VkDescriptorSetLayout from VulkanContext::VulkanDescriptorSetLayout (if the context has a new layout, invalidate all VkPipeline objects)
    VkDescriptorSetLayout LastDescriptorSetLayout = VK_NULL_HANDLE;

    VkShaderStageFlags ShaderStageFlags = 0;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;

    void* SpecConstantDataStorage = nullptr;
};

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

    [[nodiscard]] VkImageView GetImageViewForFramebuffer(VkDevice vulkanDevice, uint32_t level, uint32_t layer);

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
    const char* DebugName                   = {};

    // precached image views
    VkImageView ImageView                                   = VK_NULL_HANDLE;       // default view with all mip-levels
    VkImageView ImageViewStorage                            = VK_NULL_HANDLE;       // default view with identity swizzle (all mip-levels)
    VkImageView ImageViewForFramebuffer[EOS_MAX_MIP_LEVELS][6]  = {};                   // max 6 faces for cubemap rendering
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

struct CommandBufferData final
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

    bool IsRendering = false;
    EOS::RenderPipelineHandle CurrentGraphicsPipeline{};
    EOS::ComputePipelineHandle CurrentComputePipeline{};
    EOS::RayTracingPipelineHandle CurrentRayTracingPipeline{};

    VkPipeline LastPipelineBound = VK_NULL_HANDLE;
    EOS::Framebuffer VulkanFrameBuffer{};
};

struct DeferredTask final
{
    DeferredTask(std::packaged_task<void()>&& task, EOS::SubmitHandle handle) : Task(std::move(task)), Handle(handle) {}
    DELETE_COPY_MOVE(DeferredTask);

    std::packaged_task<void()> Task;
    EOS::SubmitHandle Handle;
};

class VulkanPipelineBuilder final
{
public:
    explicit VulkanPipelineBuilder();
    ~VulkanPipelineBuilder() = default;
    DELETE_COPY_MOVE(VulkanPipelineBuilder)


    VulkanPipelineBuilder& DynamicState(VkDynamicState state);
    VulkanPipelineBuilder& PrimitiveTypology(VkPrimitiveTopology topology);
    VulkanPipelineBuilder& RasterizationSamples(VkSampleCountFlagBits samples, float minSampleShading);
    VulkanPipelineBuilder& ShaderStage(const VkPipelineShaderStageCreateInfo &stage);
    VulkanPipelineBuilder& CullMode(VkCullModeFlags mode);
    VulkanPipelineBuilder& StencilStateOps(VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp);
    VulkanPipelineBuilder& StencilMasks(VkStencilFaceFlags faceMask, uint32_t compareMask, uint32_t writeMask, uint32_t reference);
    VulkanPipelineBuilder& FrontFace(VkFrontFace mode);
    VulkanPipelineBuilder& PolygonMode(VkPolygonMode mode);
    VulkanPipelineBuilder& VertexInputState(const VkPipelineVertexInputStateCreateInfo& state);
    VulkanPipelineBuilder& ColorAttachments(const VkPipelineColorBlendAttachmentState* states, const VkFormat* formats, uint32_t numColorAttachments);
    VulkanPipelineBuilder& DepthAttachmentFormat(VkFormat format);
    VulkanPipelineBuilder& StencilAttachmentFormat(VkFormat format);

    VulkanPipelineBuilder& PatchControlPoints(uint32_t numPoints);

    [[nodiscard]] VkResult Build(VkDevice device, VkPipelineCache pipelineCache, VkPipelineLayout pipelineLayout, VkPipeline* outPipeline, const char* debugName = nullptr) noexcept;

private:
    static constexpr int32_t MaxDynamicStates = 128;
    uint32_t NumberOfDynamicStates = 0;
    uint32_t NumberOfShaderStages = 0;
    uint32_t NumberOfColorAttachments = 0;

    VkPipelineVertexInputStateCreateInfo VertexInputStateInfo;
    VkPipelineInputAssemblyStateCreateInfo InputAssemblyState;
    VkPipelineRasterizationStateCreateInfo RasterizationState;
    VkPipelineMultisampleStateCreateInfo MultisampleState;
    VkPipelineDepthStencilStateCreateInfo DepthStencilState;
    VkPipelineTessellationStateCreateInfo TesselationState;

    VkDynamicState DynamicStates[MaxDynamicStates]{};
    VkPipelineShaderStageCreateInfo ShaderStages[static_cast<int>(EOS::ShaderStage::Fragment) + 1]{};
    VkPipelineColorBlendAttachmentState ColorBlendAttachmentStates[EOS_MAX_COLOR_ATTACHMENTS]{};

    VkFormat ColorAttachmentFormats[EOS_MAX_COLOR_ATTACHMENTS]{};
    VkFormat DepthAttachmentFormatInfo = VK_FORMAT_UNDEFINED;
    VkFormat StencilAttachmentFormatInfo = VK_FORMAT_UNDEFINED;

    static inline uint32_t NumberOfCreatedPipelines = 0;
};

class VulkanStagingDevice final
{
    struct MemoryRegionDescription
    {
        uint32_t Offset = 0;
        VkDeviceSize Size = 0;
        EOS::SubmitHandle Handle{};
    };

public:
    explicit VulkanStagingDevice(VulkanContext* context);
    ~VulkanStagingDevice() = default;
    DELETE_COPY_MOVE(VulkanStagingDevice);

    void BufferSubData(const EOS::Handle<EOS::Buffer>& buffer, size_t dstOffset, size_t size, const void* data);

private:
    void EnsureSize(uint32_t sizeNeeded);
    void WaitAndReset();
    [[nodiscard]] MemoryRegionDescription GetNextFreeOffset(uint32_t size);

private:
    VulkanContext* VkContext;
    EOS::Holder<EOS::BufferHandle> StagingBuffer;

    inline static uint8_t Alignment = 16;
    inline static constexpr uint32_t MaxBufferSize =  128 * 2048 * 2048;  // 128 mb //TODO: Get this from properties
    inline static constexpr uint32_t MinBufferSize =  4 * 2048 * 2048;    // 4 mb

    VkDeviceSize Size = 0;
    uint32_t Counter = -1;

    std::deque<MemoryRegionDescription> Regions;
};

class VulkanContext final : public EOS::IContext
{
public:
    explicit VulkanContext(const EOS::ContextCreationDescription& contextDescription);
    ~VulkanContext() override;
    DELETE_COPY_MOVE(VulkanContext)

    //Implements EOS::IContext
    [[nodiscard]] EOS::ICommandBuffer& AcquireCommandBuffer() override;
    [[nodiscard]] EOS::SubmitHandle Submit(EOS::ICommandBuffer &commandBuffer, EOS::TextureHandle present) override;
    [[nodiscard]] EOS::TextureHandle GetSwapChainTexture() override;
    [[nodiscard]] EOS::Format GetSwapchainFormat() const override;
    [[nodiscard]] EOS::Holder<EOS::ShaderModuleHandle> CreateShaderModule(const EOS::ShaderInfo &shaderInfo) override;
    [[nodiscard]] EOS::Holder<EOS::RenderPipelineHandle> CreateRenderPipeline(const EOS::RenderPipelineDescription &renderPipelineDescription) override;
    [[nodiscard]] EOS::Holder<EOS::BufferHandle> CreateBuffer(const EOS::BufferDescription &bufferDescription) override;

    void Destroy(EOS::TextureHandle handle) override;
    void Destroy(EOS::ShaderModuleHandle handle) override;
    void Destroy(EOS::RenderPipelineHandle handle) override;
    void Destroy(EOS::BufferHandle handle) override;

    void Upload(EOS::BufferHandle handle, const void* data, size_t size, size_t offset) override;

    //Deferred Tasks
    void ProcessDeferredTasks() const;
    void WaitOnDeferredTasks() const;
    void Defer(std::packaged_task<void()>&& task, EOS::SubmitHandle handle = {}) const;

    void GrowDescriptorPool(uint32_t maxTextures, uint32_t maxSamplers, uint32_t maxAccelStructs);
    void BindDefaultDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint, VkPipelineLayout layout) const;
    [[nodiscard]] VkDevice GetDevice() const;
    [[nodiscard]] EOS::BufferHandle CreateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, const char* debugName);


    std::unique_ptr<CommandPool> VulkanCommandPool = nullptr;
    VulkanRenderPipelinePool RenderPipelinePool{};
    VulkanShaderModulePool ShaderModulePool{};
    VulkanTexturePool TexturePool{};
    VulkanBufferPool BufferPool{};
    VmaAllocator vmaAllocator                       = VK_NULL_HANDLE;

private:
    [[nodiscard]] bool HasSwapChain() const noexcept;
    void CreateVulkanInstance(const char* applicationName);
    void SetupDebugMessenger();
    void CreateSurface(void* window, void* display);
    void CreateAllocator();
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
    std::unique_ptr<VulkanStagingDevice> VulkanStagingBuffer = nullptr;
    mutable std::deque<DeferredTask> DeferredTasks;
        
    VkDescriptorSetLayout DescriptorSetLayout       = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool                 = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet                   = VK_NULL_HANDLE;

    uint32_t CurrentMaxTextures;
    uint32_t CurrentMaxSamplers;
    uint32_t CurrentMaxAccelStructs;

    bool HasAccelerationStructure                   = false; //TOOD: Just check size of the pipeline pool for raytracing
    bool HasRaytracingPipeline                      = false; //TOOD: Just check size of the pipeline pool for raytracing
    bool UseStagingDevice                           = false;


    CommandBuffer CurrentCommandBuffer{};                   //TODO: This needs to become a map or vector for multithreaded recording.
    DeviceQueues VulkanDeviceQueues{};
    EOS::ContextConfiguration Configuration{};              //TODO: Should the lifetime of this obj be the whole application?

    friend struct VulkanSwapChain;
    friend struct VulkanSwapChainSupportDetails;
};