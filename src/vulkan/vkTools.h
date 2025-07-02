#pragma once
#include <cstdio>

#include <volk.h>
#include <vk_mem_alloc.h>

// https://searchfox.org/mozilla-central/source/gfx/src/X11UndefineNone.h
// The header <X11/X.h> defines "None" as a macro that expands to "0L".
// This is terrible because many enumerations have an enumerator named "None".
// To work around this, we undefine the macro "None", and define a replacement
// macro named "X11None".
// Include this header after including X11 headers, where necessary.
#ifdef None
#  undef None
#  define X11None 0L
// <X11/X.h> also defines "RevertToNone" as a macro that expands to "(int)None".
// Since we are undefining "None", that stops working. To keep it working,
// we undefine "RevertToNone" and redefine it in terms of "X11None".
#  ifdef RevertToNone
#    undef RevertToNone
#    define RevertToNone (int)X11None
#  endif
#endif

#include "enums.h"
#include "logger.h"

#define VK_ASSERT(func){ const VkResult result = func;      \
    CHECK(result == VK_SUCCESS, "Vulkan Assert failed: ");   \
}

#pragma region ForwardDeclare
struct DeviceQueues;
namespace EOS
{
    struct SpecializationConstantDescription;
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
                return pObjectName ? fmt::format("{} (Type: {}, Handle: {:#x})", pObjectName, ObjectToString(objectType), objectHandle) : fmt::format("Type: {}, Handle: {:#x}", ObjectToString(objectType), objectHandle);
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

//TODO: Move most of them back to VulkanContext -> private
namespace VkContext
{
    struct TextureFormatProperties
    {
        const EOS::Format Format = EOS::Format::Invalid;
        const uint8_t BytesPerBlock : 5 = 1;
        const uint8_t BlockWidth : 3 = 1;
        const uint8_t BlockHeight : 3 = 1;
        const uint8_t MinBlocksX : 2 = 1;
        const uint8_t MinBlocksY : 2 = 1;
        const bool Depth : 1 = false;
        const bool Stencil : 1 = false;
        const bool Compressed : 1 = false;
        const uint8_t NumberOfPlanes : 2 = 1;
    };

    static constexpr TextureFormatProperties VulkanTextureFormatProperties[]
    {
        TextureFormatProperties { .Format = EOS::Format::Invalid, .BytesPerBlock = 1 },
        TextureFormatProperties { .Format = EOS::Format::R_UN8, .BytesPerBlock = 1 },
        TextureFormatProperties { .Format = EOS::Format::R_UI16, .BytesPerBlock = 2 },
        TextureFormatProperties { .Format = EOS::Format::R_UI32, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::R_UN16, .BytesPerBlock = 2 },
        TextureFormatProperties { .Format = EOS::Format::R_F16, .BytesPerBlock = 2 },
        TextureFormatProperties { .Format = EOS::Format::R_F32, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::RG_UN8, .BytesPerBlock = 2 },
        TextureFormatProperties { .Format = EOS::Format::RG_UI16, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::RG_UI32, .BytesPerBlock = 8 },
        TextureFormatProperties { .Format = EOS::Format::RG_UN16, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::RG_F16, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::RG_F32, .BytesPerBlock = 8 },
        TextureFormatProperties { .Format = EOS::Format::RGBA_UN8, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::RGBA_UI32, .BytesPerBlock = 16 },
        TextureFormatProperties { .Format = EOS::Format::RGBA_F16, .BytesPerBlock = 8 },
        TextureFormatProperties { .Format = EOS::Format::RGBA_F32, .BytesPerBlock = 16 },
        TextureFormatProperties { .Format = EOS::Format::RGBA_SRGB8, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::BGRA_UN8, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::BGRA_SRGB8, .BytesPerBlock = 4 },
        TextureFormatProperties { .Format = EOS::Format::ETC2_RGB8, .BytesPerBlock = 8,.BlockWidth = 4, .BlockHeight = 4, .Compressed = true },
        TextureFormatProperties { .Format = EOS::Format::ETC2_SRGB8, .BytesPerBlock = 8,.BlockWidth = 4, .BlockHeight = 4, .Compressed = true },
        TextureFormatProperties { .Format = EOS::Format::BC7_RGBA, .BytesPerBlock = 16,.BlockWidth = 4, .BlockHeight = 4, .Compressed = true },
        TextureFormatProperties { .Format = EOS::Format::Z_UN16, .BytesPerBlock = 2,.Depth = true },
        TextureFormatProperties { .Format = EOS::Format::Z_UN24, .BytesPerBlock = 3,.Depth = true },
        TextureFormatProperties { .Format = EOS::Format::Z_F32, .BytesPerBlock = 4,.Depth = true },
        TextureFormatProperties { .Format = EOS::Format::Z_UN24_S_UI8, .BytesPerBlock = 4,.Depth = true, .Stencil = true },
        TextureFormatProperties { .Format = EOS::Format::Z_F32_S_UI8, .BytesPerBlock = 5,.Depth = true, .Stencil = true },
        TextureFormatProperties { .Format = EOS::Format::YUV_NV12, .BytesPerBlock = 24,.BlockWidth = 4, .BlockHeight = 4, .Compressed = true, .NumberOfPlanes = 2 }, // Subsampled 420
        TextureFormatProperties { .Format = EOS::Format::YUV_420p, .BytesPerBlock = 24,.BlockWidth = 4, .BlockHeight = 4, .Compressed = true, .NumberOfPlanes = 3 }, // Subsampled 420
    };

    [[nodiscard]] constexpr uint32_t CalculateNumberOfMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels) levels++;

        return levels;
    }
    
    [[nodiscard]] constexpr uint32_t GetNumberOfImagePlanes(VkFormat format) {
        switch (format) {
            case VK_FORMAT_UNDEFINED:
                return 0;
            case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
            case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
            case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
            case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
            case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
            case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
            case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
            case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
                return 3;
            case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
            case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
            case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
            case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
            case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
            case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
            case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
            case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
            case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
                return 2;
            default:
                return 1;
        }
    }


    [[nodiscard]] static inline bool IsDepthOrStencilFormat(EOS::Format format)
    {
        return VulkanTextureFormatProperties[format].Depth || VulkanTextureFormatProperties[format].Stencil;
    }

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

    [[nodiscard]] EOS::Format vkFormatToFormat(VkFormat format);
    [[nodiscard]] VkFormat FormatTovkFormat(EOS::Format format);
    [[nodiscard]] VkFormat VertexFormatToVkFormat(EOS::VertexFormat format);
    [[nodiscard]] std::vector<VkFormat> GetCompatibleDepthStencilFormats(EOS::Format format);
    [[nodiscard]] VkFormat GetClosestDepthStencilFormat(EOS::Format desiredFormat, const VkPhysicalDevice& physicalDevice);
    [[nodiscard]] uint32_t GetTextureBytesPerLayer(uint32_t width, uint32_t height, EOS::Format format, uint32_t level);


    [[nodiscard]] VkBlendFactor BlendFactorToVkBlendFactor(EOS::BlendFactor value);
    [[nodiscard]] VkBlendOp BlendOpToVkBlendOp(EOS::BlendOp value);

    [[nodiscard]] VkSpecializationInfo GetPipelineShaderStageSpecializationInfo(const EOS::SpecializationConstantDescription& specializationDescription,  VkSpecializationMapEntry* outEntries);
    [[nodiscard]] VkPipelineShaderStageCreateInfo GetPipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule, const char* entryPoint, const VkSpecializationInfo* specializationInfo);
    [[nodiscard]] VkStencilOp StencilOpToVkStencilOp(const EOS::StencilOp op);
    [[nodiscard]] VkCompareOp CompareOpToVkCompareOp(const EOS::CompareOp func);
    [[nodiscard]] VkPrimitiveTopology TopologyToVkPrimitiveTopology(EOS::Topology t);
    [[nodiscard]] VkPolygonMode PolygonModeToVkPolygonMode(EOS::PolygonMode mode);
    [[nodiscard]] VkCullModeFlags CullModeToVkCullMode(EOS::CullMode mode);
    [[nodiscard]] VkFrontFace WindingModeToVkFrontFace(EOS::WindingMode mode);

    [[nodiscard]] VkSampleCountFlagBits GetVulkanSampleCountFlags(uint32_t numSamples, VkSampleCountFlags maxSamplesMask);
    [[nodiscard]] uint32_t GetFramebufferMSAABitMask(VkPhysicalDevice physicalDevice);

    [[nodiscard]] VkDescriptorSetLayoutBinding GetDSLBinding(uint32_t binding, VkDescriptorType descriptorType, uint32_t descriptorCount, VkShaderStageFlags stageFlags);

    [[nodiscard]] VkAttachmentLoadOp LoadOpToVkAttachmentLoadOp(EOS::LoadOp loadOp);
    [[nodiscard]] VkAttachmentStoreOp StoreOpToVkAttachmentStoreOp(EOS::StoreOp storeOp);
    [[nodiscard]] VkBufferUsageFlags BufferUsageFlagsToVkBufferUsageFlags(EOS::BufferUsageFlags bufferUsageFlags);
    [[nodiscard]] VkMemoryPropertyFlags StorageTypeToVkMemoryPropertyFlags(EOS::StorageType storage);

    [[nodiscard]] VkIndexType IndexFormatToVkIndexType(EOS::IndexFormat indexFormat);
}

namespace VkSynchronization
{
    [[nodiscard]] VkSemaphore CreateSemaphore(const VkDevice& device, const char* debugName);
    [[nodiscard]] VkSemaphore CreateSemaphoreTimeline(const VkDevice& device, uint64_t initialValue, const char* debugName);
    [[nodiscard]] VkFence CreateFence(const VkDevice& device, const char* debugName);
    VkPipelineStageFlags2 ConvertToVkPipelineStage2(const EOS::ResourceState& state);
    VkAccessFlags2 ConvertToVkAccessFlags2(const EOS::ResourceState& state);
    VkImageLayout ConvertToVkImageLayout(const EOS::ResourceState& state);
    VkImageAspectFlags ConvertToVkImageAspectFlags(const EOS::ResourceState& state);
}