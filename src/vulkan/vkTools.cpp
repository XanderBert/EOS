#include <vector>

#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION

#include <ranges>

#include "vkTools.h"
#include "vulkanClasses.h"


VkResult SetDebugObjectName(const VkDevice device, const VkObjectType type, const uint64_t handle, const char* name)
{
    if (!name || !*name) { return VK_SUCCESS; }

    const VkDebugUtilsObjectNameInfoEXT nameInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };

    return vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
}

uint32_t FindQueueFamilyIndex(VkPhysicalDevice physicalDevice, VkQueueFlags flags)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> properties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, properties.data());

    auto findDedicatedQueueFamilyIndex = [&properties](VkQueueFlags require, VkQueueFlags avoid) -> uint32_t
    {
        for (uint32_t i{}; i != properties.size(); ++i)
        {
            const bool isSuitable = (properties[i].queueFlags & require) == require;
            const bool isDedicated = (properties[i].queueFlags & avoid) == 0;
            if (properties[i].queueCount && isSuitable && isDedicated) { return i; }
        }

        return DeviceQueues::InvalidIndex;
    };

    // dedicated queue for compute
    if (flags & VK_QUEUE_COMPUTE_BIT)
    {
        const uint32_t q = findDedicatedQueueFamilyIndex(flags, VK_QUEUE_GRAPHICS_BIT);
        if (q != DeviceQueues::InvalidIndex) { return q; }
    }

    // dedicated queue for transfer
    if (flags & VK_QUEUE_TRANSFER_BIT)
    {
        const uint32_t q = findDedicatedQueueFamilyIndex(flags, VK_QUEUE_GRAPHICS_BIT);
        if (q != DeviceQueues::InvalidIndex) { return q; }
    }

    // any suitable
    return findDedicatedQueueFamilyIndex(flags, 0);
}


void CheckMissingDeviceFeatures(
    const VkPhysicalDeviceFeatures& deviceFeatures10, const VkPhysicalDeviceFeatures2& vkFeatures10,
    const VkPhysicalDeviceVulkan11Features& deviceFeatures11, const VkPhysicalDeviceVulkan11Features& vkFeatures11,
    const VkPhysicalDeviceVulkan12Features& deviceFeatures12, const VkPhysicalDeviceVulkan12Features& vkFeatures12,
    const VkPhysicalDeviceVulkan13Features& deviceFeatures13, const VkPhysicalDeviceVulkan13Features& vkFeatures13)
{
    std::string missingFeatures;
#define CHECK_VULKAN_FEATURE(reqFeatures, availFeatures, feature, version) if ((reqFeatures.feature == VK_TRUE) && (availFeatures.feature == VK_FALSE)) missingFeatures.append("\n   " version " ." #feature);
#define CHECK_FEATURE_1_0(feature) CHECK_VULKAN_FEATURE(deviceFeatures10, vkFeatures10.features, feature, "1.0 ");
        CHECK_FEATURE_1_0(robustBufferAccess);
        CHECK_FEATURE_1_0(fullDrawIndexUint32);
        CHECK_FEATURE_1_0(imageCubeArray);
        CHECK_FEATURE_1_0(independentBlend);
        CHECK_FEATURE_1_0(geometryShader);
        CHECK_FEATURE_1_0(tessellationShader);
        CHECK_FEATURE_1_0(sampleRateShading);
        CHECK_FEATURE_1_0(dualSrcBlend);
        CHECK_FEATURE_1_0(logicOp);
        CHECK_FEATURE_1_0(multiDrawIndirect);
        CHECK_FEATURE_1_0(drawIndirectFirstInstance);
        CHECK_FEATURE_1_0(depthClamp);
        CHECK_FEATURE_1_0(depthBiasClamp);
        CHECK_FEATURE_1_0(fillModeNonSolid);
        CHECK_FEATURE_1_0(depthBounds);
        CHECK_FEATURE_1_0(wideLines);
        CHECK_FEATURE_1_0(largePoints);
        CHECK_FEATURE_1_0(alphaToOne);
        CHECK_FEATURE_1_0(multiViewport);
        CHECK_FEATURE_1_0(samplerAnisotropy);
        CHECK_FEATURE_1_0(textureCompressionETC2);
        CHECK_FEATURE_1_0(textureCompressionASTC_LDR);
        CHECK_FEATURE_1_0(textureCompressionBC);
        CHECK_FEATURE_1_0(occlusionQueryPrecise);
        CHECK_FEATURE_1_0(pipelineStatisticsQuery);
        CHECK_FEATURE_1_0(vertexPipelineStoresAndAtomics);
        CHECK_FEATURE_1_0(fragmentStoresAndAtomics);
        CHECK_FEATURE_1_0(shaderTessellationAndGeometryPointSize);
        CHECK_FEATURE_1_0(shaderImageGatherExtended);
        CHECK_FEATURE_1_0(shaderStorageImageExtendedFormats);
        CHECK_FEATURE_1_0(shaderStorageImageMultisample);
        CHECK_FEATURE_1_0(shaderStorageImageReadWithoutFormat);
        CHECK_FEATURE_1_0(shaderStorageImageWriteWithoutFormat);
        CHECK_FEATURE_1_0(shaderUniformBufferArrayDynamicIndexing);
        CHECK_FEATURE_1_0(shaderSampledImageArrayDynamicIndexing);
        CHECK_FEATURE_1_0(shaderStorageBufferArrayDynamicIndexing);
        CHECK_FEATURE_1_0(shaderStorageImageArrayDynamicIndexing);
        CHECK_FEATURE_1_0(shaderClipDistance);
        CHECK_FEATURE_1_0(shaderCullDistance);
        CHECK_FEATURE_1_0(shaderFloat64);
        CHECK_FEATURE_1_0(shaderInt64);
        CHECK_FEATURE_1_0(shaderInt16);
        CHECK_FEATURE_1_0(shaderResourceResidency);
        CHECK_FEATURE_1_0(shaderResourceMinLod);
        CHECK_FEATURE_1_0(sparseBinding);
        CHECK_FEATURE_1_0(sparseResidencyBuffer);
        CHECK_FEATURE_1_0(sparseResidencyImage2D);
        CHECK_FEATURE_1_0(sparseResidencyImage3D);
        CHECK_FEATURE_1_0(sparseResidency2Samples);
        CHECK_FEATURE_1_0(sparseResidency4Samples);
        CHECK_FEATURE_1_0(sparseResidency8Samples);
        CHECK_FEATURE_1_0(sparseResidency16Samples);
        CHECK_FEATURE_1_0(sparseResidencyAliased);
        CHECK_FEATURE_1_0(variableMultisampleRate);
        CHECK_FEATURE_1_0(inheritedQueries);
#undef CHECK_FEATURE_1_0
#define CHECK_FEATURE_1_1(feature) CHECK_VULKAN_FEATURE(deviceFeatures11, vkFeatures11, feature, "1.1 ");
        CHECK_FEATURE_1_1(storageBuffer16BitAccess);
        CHECK_FEATURE_1_1(uniformAndStorageBuffer16BitAccess);
        CHECK_FEATURE_1_1(storagePushConstant16);
        CHECK_FEATURE_1_1(storageInputOutput16);
        CHECK_FEATURE_1_1(multiview);
        CHECK_FEATURE_1_1(multiviewGeometryShader);
        CHECK_FEATURE_1_1(multiviewTessellationShader);
        CHECK_FEATURE_1_1(variablePointersStorageBuffer);
        CHECK_FEATURE_1_1(variablePointers);
        CHECK_FEATURE_1_1(protectedMemory);
        CHECK_FEATURE_1_1(samplerYcbcrConversion);
        CHECK_FEATURE_1_1(shaderDrawParameters);
#undef CHECK_FEATURE_1_1
#define CHECK_FEATURE_1_2(feature) CHECK_VULKAN_FEATURE(deviceFeatures12, vkFeatures12, feature, "1.2 ");
        CHECK_FEATURE_1_2(samplerMirrorClampToEdge);
        CHECK_FEATURE_1_2(drawIndirectCount);
        CHECK_FEATURE_1_2(storageBuffer8BitAccess);
        CHECK_FEATURE_1_2(uniformAndStorageBuffer8BitAccess);
        CHECK_FEATURE_1_2(storagePushConstant8);
        CHECK_FEATURE_1_2(shaderBufferInt64Atomics);
        CHECK_FEATURE_1_2(shaderSharedInt64Atomics);
        CHECK_FEATURE_1_2(shaderFloat16);
        CHECK_FEATURE_1_2(shaderInt8);
        CHECK_FEATURE_1_2(descriptorIndexing);
        CHECK_FEATURE_1_2(shaderInputAttachmentArrayDynamicIndexing);
        CHECK_FEATURE_1_2(shaderUniformTexelBufferArrayDynamicIndexing);
        CHECK_FEATURE_1_2(shaderStorageTexelBufferArrayDynamicIndexing);
        CHECK_FEATURE_1_2(shaderUniformBufferArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderSampledImageArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderStorageBufferArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderStorageImageArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderInputAttachmentArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderUniformTexelBufferArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(shaderStorageTexelBufferArrayNonUniformIndexing);
        CHECK_FEATURE_1_2(descriptorBindingUniformBufferUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingSampledImageUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingStorageImageUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingStorageBufferUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingUniformTexelBufferUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingStorageTexelBufferUpdateAfterBind);
        CHECK_FEATURE_1_2(descriptorBindingUpdateUnusedWhilePending);
        CHECK_FEATURE_1_2(descriptorBindingPartiallyBound);
        CHECK_FEATURE_1_2(descriptorBindingVariableDescriptorCount);
        CHECK_FEATURE_1_2(runtimeDescriptorArray);
        CHECK_FEATURE_1_2(samplerFilterMinmax);
        CHECK_FEATURE_1_2(scalarBlockLayout);
        CHECK_FEATURE_1_2(imagelessFramebuffer);
        CHECK_FEATURE_1_2(uniformBufferStandardLayout);
        CHECK_FEATURE_1_2(shaderSubgroupExtendedTypes);
        CHECK_FEATURE_1_2(separateDepthStencilLayouts);
        CHECK_FEATURE_1_2(hostQueryReset);
        CHECK_FEATURE_1_2(timelineSemaphore);
        CHECK_FEATURE_1_2(bufferDeviceAddress);
        CHECK_FEATURE_1_2(bufferDeviceAddressCaptureReplay);
        CHECK_FEATURE_1_2(bufferDeviceAddressMultiDevice);
        CHECK_FEATURE_1_2(vulkanMemoryModel);
        CHECK_FEATURE_1_2(vulkanMemoryModelDeviceScope);
        CHECK_FEATURE_1_2(vulkanMemoryModelAvailabilityVisibilityChains);
        CHECK_FEATURE_1_2(shaderOutputViewportIndex);
        CHECK_FEATURE_1_2(shaderOutputLayer);
        CHECK_FEATURE_1_2(subgroupBroadcastDynamicId);
#undef CHECK_FEATURE_1_2
#define CHECK_FEATURE_1_3(feature) CHECK_VULKAN_FEATURE(deviceFeatures13, vkFeatures13, feature, "1.3 ");
        CHECK_FEATURE_1_3(robustImageAccess);
        CHECK_FEATURE_1_3(inlineUniformBlock);
        CHECK_FEATURE_1_3(descriptorBindingInlineUniformBlockUpdateAfterBind);
        CHECK_FEATURE_1_3(pipelineCreationCacheControl);
        CHECK_FEATURE_1_3(privateData);
        CHECK_FEATURE_1_3(shaderDemoteToHelperInvocation);
        CHECK_FEATURE_1_3(shaderTerminateInvocation);
        CHECK_FEATURE_1_3(subgroupSizeControl);
        CHECK_FEATURE_1_3(computeFullSubgroups);
        CHECK_FEATURE_1_3(synchronization2);
        CHECK_FEATURE_1_3(textureCompressionASTC_HDR);
        CHECK_FEATURE_1_3(shaderZeroInitializeWorkgroupMemory);
        CHECK_FEATURE_1_3(dynamicRendering);
        CHECK_FEATURE_1_3(shaderIntegerDotProduct);
        CHECK_FEATURE_1_3(maintenance4);
#undef CHECK_FEATURE_1_3
    if (!missingFeatures.empty())
    {
        EOS::Logger->warn("Missing Vulkan features: {}", missingFeatures.c_str());
    }
}

void SelectHardwareDevice(const std::vector<EOS::HardwareDeviceDescription>& hardwareDevices, VkPhysicalDevice& physicalDevice)
{
    //TODO: Do more check -> Maybe user can specify on what conditions it should be selected?
    physicalDevice = reinterpret_cast<VkPhysicalDevice>(hardwareDevices.back().id);
    EOS::Logger->info("Selected Hardware Device: {}", hardwareDevices.back().name.c_str());
}

void GetDeviceExtensions(std::vector<VkExtensionProperties>& deviceExtensions,const VkPhysicalDevice& vulkanPhysicalDevice, const char* forValidationLayer)
{
    uint32_t numExtensions{0};
    vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, forValidationLayer, &numExtensions, nullptr);

    deviceExtensions.clear();
    deviceExtensions.resize(numExtensions);

    vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, forValidationLayer, &numExtensions, deviceExtensions.data());
}

void PrintDeviceExtensions(const VkPhysicalDevice& vulkanPhysicalDevice, std::vector<VkExtensionProperties>& allDeviceExtensions)
{
    std::vector<VkExtensionProperties> deviceExtensions;
    GetDeviceExtensions(deviceExtensions, vulkanPhysicalDevice);
    std::vector<VkExtensionProperties> deviceExtensionsForValidationLayer;
    GetDeviceExtensions(deviceExtensionsForValidationLayer, vulkanPhysicalDevice, validationLayer);

    EOS::Logger->info("Device Extensions:\n     {}", fmt::join(deviceExtensions | std::views::transform([](const auto& ext) { return ext.extensionName; }), "\n     "));
    EOS::Logger->info("Device Extension For Validation Layer:\n     {}", fmt::join(deviceExtensionsForValidationLayer | std::views::transform([](const auto& ext) { return ext.extensionName; }), "\n     "));

    //Merge the two
    allDeviceExtensions.clear();
    allDeviceExtensions.resize(deviceExtensions.size() + deviceExtensionsForValidationLayer.size());

    //TODO: Insert all data from deviceExtensions and deviceExtensionForValidationLayer into allDeviceExtensions
    std::ranges::copy(deviceExtensions, allDeviceExtensions.begin());
}

void GetPhysicalDeviceProperties(VkPhysicalDeviceProperties2& physicalDeviceProperties, VkPhysicalDeviceDriverProperties& physicalDeviceDriverProperties, VkPhysicalDevice physicalDevice)
{
    //TODO make 1.4 compatible
    //Get properties
    physicalDeviceDriverProperties    = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES, nullptr};
    //VkPhysicalDeviceVulkan14Properties  vkPhysicalDeviceVulkan14Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES,&vkPhysicalDeviceDriverProperties,};
    VkPhysicalDeviceVulkan13Properties  vkPhysicalDeviceVulkan13Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,&physicalDeviceDriverProperties,};
    VkPhysicalDeviceVulkan12Properties  vkPhysicalDeviceVulkan12Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,&vkPhysicalDeviceVulkan13Properties,};
    VkPhysicalDeviceVulkan11Properties  vkPhysicalDeviceVulkan11Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,&vkPhysicalDeviceVulkan12Properties,};

    physicalDeviceProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&vkPhysicalDeviceVulkan11Properties,VkPhysicalDeviceProperties{},};
    vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);
}

VkSurfaceFormatKHR GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace)
{
    //TODO: Look into VkSurfaceFormat2KHR -> this enables Compression of the swapChain image
    //https://docs.vulkan.org/samples/latest/samples/performance/image_compression_control/README.html

    //Non Linear is the default
    VkSurfaceFormatKHR preferred{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    if (desiredColorSpace == EOS::ColorSpace::SRGB_Linear)
    {
        // VK_COLOR_SPACE_BT709_LINEAR_EXT is the closest space to linear
        preferred  = VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_BT709_LINEAR_EXT};
    }

    //Check if we have a combination with our desired format & color space
    for (const VkSurfaceFormatKHR& fmt : availableFormats)
    {
        if (fmt.format == preferred.format && fmt.colorSpace == preferred.colorSpace) { return fmt; }
    }

    // if we can't find a matching format and color space, fallback on matching only format
    for (const VkSurfaceFormatKHR& fmt : availableFormats)
    {
        if (fmt.format == preferred.format) { return fmt; }
    }

    //If we still haven't found a format we just pick the first available option
    return availableFormats[0];
}

VkSemaphore CreateSemaphore(VkDevice device, const char* debugName)
{
    constexpr VkSemaphoreCreateInfo createInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .flags = 0,
    };

    VkSemaphore semaphore = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));
    VK_ASSERT(SetDebugObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(semaphore), debugName));

    return semaphore;
}