#include <vector>
#include <ranges>

#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION

#include "vkTools.h"
#include "vulkanClasses.h"

namespace VkDebug
{
    const char* ObjectToString(const VkObjectType objectType)
    {
        switch (objectType)
        {
            case VK_OBJECT_TYPE_UNKNOWN: return "Unknown";
            case VK_OBJECT_TYPE_INSTANCE: return "Instance";
            case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "PhysicalDevice";
            case VK_OBJECT_TYPE_DEVICE: return "Device";
            case VK_OBJECT_TYPE_QUEUE: return "Queue";
            case VK_OBJECT_TYPE_SEMAPHORE: return "Semaphore";
            case VK_OBJECT_TYPE_COMMAND_BUFFER: return "CommandBuffer";
            case VK_OBJECT_TYPE_FENCE: return "Fence";
            case VK_OBJECT_TYPE_DEVICE_MEMORY: return "DeviceMemory";
            case VK_OBJECT_TYPE_BUFFER: return "Buffer";
            case VK_OBJECT_TYPE_IMAGE: return "Image";
            case VK_OBJECT_TYPE_EVENT: return "Event";
            case VK_OBJECT_TYPE_QUERY_POOL: return "QueryPool";
            case VK_OBJECT_TYPE_BUFFER_VIEW: return "BufferView";
            case VK_OBJECT_TYPE_IMAGE_VIEW: return "ImageView";
            case VK_OBJECT_TYPE_SHADER_MODULE: return "ShaderModule";
            case VK_OBJECT_TYPE_PIPELINE_CACHE: return "PipelineCache";
            case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "PipelineLayout";
            case VK_OBJECT_TYPE_RENDER_PASS: return "RenderPass";
            case VK_OBJECT_TYPE_PIPELINE: return "Pipeline";
            case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "DescriptorSetLayout";
            case VK_OBJECT_TYPE_SAMPLER: return "Sampler";
            case VK_OBJECT_TYPE_DESCRIPTOR_POOL: return "DescriptorPool";
            case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "DescriptorSet";
            case VK_OBJECT_TYPE_FRAMEBUFFER: return "Framebuffer";
            case VK_OBJECT_TYPE_COMMAND_POOL: return "CommandPool";
            case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION: return "SamplerYcbcrConversion";
            case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE: return "DescriptorUpdateTemplate";
            case VK_OBJECT_TYPE_SURFACE_KHR: return "SurfaceKHR";
            case VK_OBJECT_TYPE_SWAPCHAIN_KHR: return "SwapchainKHR";
            case VK_OBJECT_TYPE_DISPLAY_KHR: return "DisplayKHR";
            case VK_OBJECT_TYPE_DISPLAY_MODE_KHR: return "DisplayModeKHR";
            case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT: return "DebugReportCallbackEXT";
            case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT: return "DebugUtilsMessengerEXT";
            case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR: return "AccelerationStructureKHR";
            case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT: return "ValidationCacheEXT";
            case VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL: return "PerformanceConfigurationINTEL";
            case VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR: return "DeferredOperationKHR";
            case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV: return "IndirectCommandsLayoutNV";
            case VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT: return "PrivateDataSlotEXT";
            case VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA: return "BufferCollectionFUCHSIA";
            default: return "UnknownType";
        }
    }

    VkResult SetDebugObjectName(const VkDevice& device, const VkObjectType& type, const uint64_t handle, const char* name)
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
}

namespace VkContext
{
    uint32_t FindQueueFamilyIndex(const VkPhysicalDevice& physicalDevice, VkQueueFlags flags)
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

            return DeviceQueueIndex::InvalidIndex;
        };

        // dedicated queue for compute
        if (flags & VK_QUEUE_COMPUTE_BIT)
        {
            const uint32_t q = findDedicatedQueueFamilyIndex(flags, VK_QUEUE_GRAPHICS_BIT);
            if (q != DeviceQueueIndex::InvalidIndex) { return q; }
        }

        // dedicated queue for transfer
        if (flags & VK_QUEUE_TRANSFER_BIT)
        {
            const uint32_t q = findDedicatedQueueFamilyIndex(flags, VK_QUEUE_GRAPHICS_BIT);
            if (q != DeviceQueueIndex::InvalidIndex) { return q; }
        }

        // any suitable
        return findDedicatedQueueFamilyIndex(flags, 0);
    }


    void CheckMissingDeviceFeatures(
        const VkPhysicalDeviceFeatures& deviceFeatures10, const VkPhysicalDeviceFeatures2& vkFeatures10,
        const VkPhysicalDeviceVulkan11Features& deviceFeatures11, const VkPhysicalDeviceVulkan11Features& vkFeatures11,
        const VkPhysicalDeviceVulkan12Features& deviceFeatures12, const VkPhysicalDeviceVulkan12Features& vkFeatures12,
        const VkPhysicalDeviceVulkan13Features& deviceFeatures13, const VkPhysicalDeviceVulkan13Features& vkFeatures13,
        const std::optional<VkPhysicalDeviceVulkan14Features>& deviceFeatures14, const std::optional<VkPhysicalDeviceVulkan14Features>& vkFeatures14)
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


    if (deviceFeatures14.has_value() && vkFeatures14.has_value())
    {
    #define CHECK_FEATURE_1_4(feature) CHECK_VULKAN_FEATURE(deviceFeatures14.value(), vkFeatures14.value(), feature, "1.4 ");
        CHECK_FEATURE_1_4(globalPriorityQuery);
        CHECK_FEATURE_1_4(shaderSubgroupRotate);
        CHECK_FEATURE_1_4(shaderSubgroupRotateClustered);
        CHECK_FEATURE_1_4(shaderFloatControls2);
        CHECK_FEATURE_1_4(shaderExpectAssume);
        CHECK_FEATURE_1_4(rectangularLines);
        CHECK_FEATURE_1_4(bresenhamLines);
        CHECK_FEATURE_1_4(smoothLines);
        CHECK_FEATURE_1_4(stippledRectangularLines);
        CHECK_FEATURE_1_4(stippledBresenhamLines);
        CHECK_FEATURE_1_4(stippledSmoothLines);
        CHECK_FEATURE_1_4(vertexAttributeInstanceRateDivisor);
        CHECK_FEATURE_1_4(vertexAttributeInstanceRateZeroDivisor);
        CHECK_FEATURE_1_4(indexTypeUint8);
        CHECK_FEATURE_1_4(dynamicRenderingLocalRead);
        CHECK_FEATURE_1_4(maintenance5);
        CHECK_FEATURE_1_4(maintenance6);
        CHECK_FEATURE_1_4(pipelineProtectedAccess);
        CHECK_FEATURE_1_4(pipelineRobustness);
        CHECK_FEATURE_1_4(hostImageCopy);
        CHECK_FEATURE_1_4(pushDescriptor);
    #undef CHECK_FEATURE_1_4
    }
        if (!missingFeatures.empty())
        {
            EOS::Logger->error("Missing Vulkan features:\n {}", missingFeatures.c_str());
        }
    }

    void SelectHardwareDevice(const std::vector<EOS::HardwareDeviceDescription>& hardwareDevices, VkPhysicalDevice& physicalDevice)
    {
        CHECK_RETURN(!hardwareDevices.empty(), "No Vulkan devices available!");

        // Scoring criteria weights
        constexpr int MIN_VIDEO_MEMORY = 1024 * 1024 * 1024; // 1GB

        struct DeviceScore
        {
            const EOS::HardwareDeviceDescription* desc  = nullptr;
            int score                                   = 0;
            bool suitable                               = false;
        };
        std::vector<DeviceScore> scoredDevices;
        scoredDevices.reserve(hardwareDevices.size());

        // Give Score all devices
        for (const auto& device : hardwareDevices)
        {
            constexpr int DEVICE_TYPE_WEIGHT = 1000;
            const auto vkDevice = reinterpret_cast<VkPhysicalDevice>(device.id);
            DeviceScore scoreEntry{&device};

            //Check device type
            switch (device.type)
            {
                case EOS::HardwareDeviceType::Discrete:
                    scoreEntry.score += DEVICE_TYPE_WEIGHT * 4;
                    break;
                case EOS::HardwareDeviceType::Integrated:
                    scoreEntry.score += DEVICE_TYPE_WEIGHT * 3;
                    break;
                case EOS::HardwareDeviceType::Virtual:
                    scoreEntry.score += DEVICE_TYPE_WEIGHT * 2;
                    break;
                case EOS::HardwareDeviceType::Software:
                    scoreEntry.score += DEVICE_TYPE_WEIGHT * 1;
                    break;
            }

            //TODO: Check Vulkan features


            // Memory check
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(vkDevice, &memProps);
            uint64_t deviceLocalMemory = 0;
            for (uint32_t i{}; i < memProps.memoryHeapCount; ++i)
            {
                if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                {
                    deviceLocalMemory += memProps.memoryHeaps[i].size;
                }
            }

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(vkDevice, &props);

            // suitability check
            scoreEntry.suitable = (deviceLocalMemory >= MIN_VIDEO_MEMORY) && (props.apiVersion >= VK_API_VERSION_1_3);

            // Additional scoring
            if (scoreEntry.suitable)
            {
                // Prefer newer devices
                scoreEntry.score += props.apiVersion;

                // Prefer more memory
                scoreEntry.score += static_cast<int>(deviceLocalMemory / (1024 * 1024)); // 1 point per MB
            }

            scoredDevices.emplace_back(scoreEntry);
        }

        // Find best device
        auto bestDevice = std::ranges::max_element(scoredDevices,[](const auto& a, const auto& b)
        {
            // Prioritize suitability first
            if (a.suitable != b.suitable){ return !a.suitable; }

            return a.score < b.score;
        });

        if (bestDevice != scoredDevices.end() && bestDevice->suitable)
        {
            physicalDevice = reinterpret_cast<VkPhysicalDevice>(bestDevice->desc->id);

            EOS::Logger->info("Selected device: {} (Score: {})", bestDevice->desc->name, bestDevice->score);
        } else
        {
            // Fallback to first device with warning
            physicalDevice = reinterpret_cast<VkPhysicalDevice>(hardwareDevices.back().id);
            EOS::Logger->warn("No optimal device found! Using fallback: {}", hardwareDevices.back().name);
        }
    }

    void GetDeviceExtensions(std::vector<VkExtensionProperties>& deviceExtensions,const VkPhysicalDevice& vulkanPhysicalDevice, const char* forValidationLayer)
    {
        uint32_t numExtensions{0};
        vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, forValidationLayer, &numExtensions, nullptr);

        deviceExtensions.clear();
        deviceExtensions.resize(numExtensions);

        vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, forValidationLayer, &numExtensions, deviceExtensions.data());
    }

    void GetDeviceExtensions(const VkPhysicalDevice& vulkanPhysicalDevice, std::vector<VkExtensionProperties>& allDeviceExtensions)
    {
        std::vector<VkExtensionProperties> deviceExtensions;
        GetDeviceExtensions(deviceExtensions, vulkanPhysicalDevice);
        std::vector<VkExtensionProperties> deviceExtensionsForValidationLayer;
        GetDeviceExtensions(deviceExtensionsForValidationLayer, vulkanPhysicalDevice, validationLayer);

        allDeviceExtensions.clear();
        allDeviceExtensions.reserve(deviceExtensions.size() + deviceExtensionsForValidationLayer.size());  // Reserve space for efficiency

        // Merge the two vectors
        allDeviceExtensions.insert(allDeviceExtensions.end(), deviceExtensions.begin(), deviceExtensions.end());
        allDeviceExtensions.insert(allDeviceExtensions.end(), deviceExtensionsForValidationLayer.begin(),deviceExtensionsForValidationLayer.end());

        EOS::Logger->debug("Device Extensions:\n     {}", fmt::join(allDeviceExtensions | std::views::transform([](const auto& ext) { return ext.extensionName; }), "\n     "));
    }

    void GetPhysicalDeviceProperties(VkPhysicalDeviceProperties2& physicalDeviceProperties, VkPhysicalDeviceDriverProperties& physicalDeviceDriverProperties, VkPhysicalDevice physicalDevice, uint32_t SDKMinorVersion)
    {
        //Get properties
        physicalDeviceDriverProperties    = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES, nullptr};
        VkPhysicalDeviceVulkan14Properties  vkPhysicalDeviceVulkan14Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES, nullptr,};
        VkPhysicalDeviceVulkan13Properties  vkPhysicalDeviceVulkan13Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,&physicalDeviceDriverProperties,};
        VkPhysicalDeviceVulkan12Properties  vkPhysicalDeviceVulkan12Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,&vkPhysicalDeviceVulkan13Properties,};
        VkPhysicalDeviceVulkan11Properties  vkPhysicalDeviceVulkan11Properties  = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,&vkPhysicalDeviceVulkan12Properties,};

        physicalDeviceProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&vkPhysicalDeviceVulkan11Properties,VkPhysicalDeviceProperties{},};

        if (SDKMinorVersion == 4)
        {
            vkPhysicalDeviceVulkan13Properties.pNext = &vkPhysicalDeviceVulkan14Properties;
            vkPhysicalDeviceVulkan14Properties.pNext = &physicalDeviceDriverProperties;
        }

        vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);
    }

    void CreateVulkanDevice(VkDevice& device, const VkPhysicalDevice& physicalDevice, DeviceQueues& deviceQueues)
    {
        CHECK(physicalDevice != VK_NULL_HANDLE, "Cannot Create a Vulkan Device if the Physical Device is not valid");

        //Device Properties
        VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2;
        VkPhysicalDeviceDriverProperties vkPhysicalDeviceDriverProperties;

        uint32_t SDKApiVersion{};
        vkEnumerateInstanceVersion(&SDKApiVersion);
        const uint32_t SDKMinor = VK_API_VERSION_MINOR(SDKApiVersion);

        GetPhysicalDeviceProperties(vkPhysicalDeviceProperties2, vkPhysicalDeviceDriverProperties, physicalDevice, SDKMinor);
        const uint32_t driverAPIVersion = vkPhysicalDeviceProperties2.properties.apiVersion;

        EOS::Logger->info("Vulkan Driver Version: {}.{}.{}", VK_API_VERSION_MAJOR(driverAPIVersion), VK_API_VERSION_MINOR(driverAPIVersion), VK_API_VERSION_PATCH(driverAPIVersion));
        EOS::Logger->info("Vulkan SDK Version: {}.{}.{}", VK_API_VERSION_MAJOR(SDKApiVersion), VK_API_VERSION_MINOR(SDKApiVersion), VK_API_VERSION_PATCH(SDKApiVersion));

        EOS::Logger->info("Driver info: {} {}", vkPhysicalDeviceDriverProperties.driverName, vkPhysicalDeviceDriverProperties.driverInfo);

        //Check if we are < 1.3 , 1.3 or 1.4
        enum class vkVersion : uint8_t
        {
            VERSION_13,
            VERSION_14,
        };

        const uint32_t driverMinor = VK_API_VERSION_MINOR(driverAPIVersion);
        CHECK(SDKMinor >= 3 && driverMinor >= 3, "For now we only support Vulkan Versions 1.3 and up.\nTry Updating your graphics drivers and/or install the latest Vulkan SDK");

        vkVersion version = vkVersion::VERSION_13;
        if (SDKMinor == 4 || driverMinor == 4){ version = vkVersion::VERSION_14; }



        //Get Features
        VkPhysicalDeviceVulkan14Features vkFeatures14       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext =  nullptr};
        VkPhysicalDeviceVulkan13Features vkFeatures13       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = nullptr};
        VkPhysicalDeviceVulkan12Features vkFeatures12       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &vkFeatures13};
        VkPhysicalDeviceVulkan11Features vkFeatures11       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &vkFeatures12};
        VkPhysicalDeviceFeatures2 startOfDeviceFeaturespNextChain = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &vkFeatures11};
        if (version == vkVersion::VERSION_14) { vkFeatures13.pNext = &vkFeatures14; }
        vkGetPhysicalDeviceFeatures2(physicalDevice, &startOfDeviceFeaturespNextChain);

        //Setup the desired features then we check if our device supports these desired features.
        VkPhysicalDeviceFeatures deviceFeatures10 =
        {
            .geometryShader                 = startOfDeviceFeaturespNextChain.features.geometryShader,
            .tessellationShader             = startOfDeviceFeaturespNextChain.features.tessellationShader,
            .sampleRateShading              = VK_TRUE,
            .multiDrawIndirect              = VK_TRUE,
            .drawIndirectFirstInstance      = VK_TRUE,
            .depthBiasClamp                 = VK_TRUE,
            .fillModeNonSolid               = startOfDeviceFeaturespNextChain.features.fillModeNonSolid,
            .samplerAnisotropy              = VK_TRUE,
            .textureCompressionBC           = startOfDeviceFeaturespNextChain.features.textureCompressionBC,
            .vertexPipelineStoresAndAtomics = startOfDeviceFeaturespNextChain.features.vertexPipelineStoresAndAtomics,
            .fragmentStoresAndAtomics       = VK_TRUE,
            .shaderImageGatherExtended      = VK_TRUE,
            .shaderInt64                    = startOfDeviceFeaturespNextChain.features.shaderInt64,
        };

        VkPhysicalDeviceVulkan11Features deviceFeatures11 =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = nullptr,           //Additional DeviceExtensionFeatures can be added here
            .storageBuffer16BitAccess   = VK_TRUE,
            .samplerYcbcrConversion     = vkFeatures11.samplerYcbcrConversion,
            .shaderDrawParameters       = VK_TRUE,
        };

        VkPhysicalDeviceVulkan12Features deviceFeatures12 =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &deviceFeatures11,
            .drawIndirectCount                              = vkFeatures12.drawIndirectCount,
            .storageBuffer8BitAccess                        = vkFeatures12.storageBuffer8BitAccess,
            .uniformAndStorageBuffer8BitAccess              = vkFeatures12.uniformAndStorageBuffer8BitAccess,
            .shaderFloat16                                  = vkFeatures12.shaderFloat16,
            .descriptorIndexing                             = VK_TRUE,
            .shaderSampledImageArrayNonUniformIndexing      = VK_TRUE,
            .descriptorBindingSampledImageUpdateAfterBind   = VK_TRUE,
            .descriptorBindingStorageImageUpdateAfterBind   = VK_TRUE,
            .descriptorBindingUpdateUnusedWhilePending      = VK_TRUE,
            .descriptorBindingPartiallyBound                = VK_TRUE,
            .descriptorBindingVariableDescriptorCount       = VK_TRUE,
            .runtimeDescriptorArray                         = VK_TRUE,
            .scalarBlockLayout                              = VK_TRUE,
            .uniformBufferStandardLayout                    = VK_TRUE,
            .hostQueryReset                                 = vkFeatures12.hostQueryReset,
            .timelineSemaphore                              = VK_TRUE,
            .bufferDeviceAddress                            = VK_TRUE,
            .vulkanMemoryModel                              = vkFeatures12.vulkanMemoryModel,
            .vulkanMemoryModelDeviceScope                   = vkFeatures12.vulkanMemoryModelDeviceScope,
        };

        VkPhysicalDeviceVulkan13Features deviceFeatures13 =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &deviceFeatures12,
            .subgroupSizeControl  = VK_TRUE,
            .synchronization2     = VK_TRUE,
            .dynamicRendering     = VK_TRUE,
            .maintenance4         = VK_TRUE,
        };




        void* createInfoNext = nullptr;
        if (version == vkVersion::VERSION_14)
        {
            VkPhysicalDeviceVulkan14Features deviceFeatures14 =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                .pNext = &deviceFeatures13,
                .globalPriorityQuery                    = vkFeatures14.globalPriorityQuery,
                .shaderSubgroupRotate                   = vkFeatures14.shaderSubgroupRotate,
                .shaderSubgroupRotateClustered          = vkFeatures14.shaderSubgroupRotateClustered,
                .shaderFloatControls2                   = vkFeatures14.shaderFloatControls2,
                .shaderExpectAssume                     = vkFeatures14.shaderExpectAssume,
                .rectangularLines                       = vkFeatures14.rectangularLines,
                .bresenhamLines                         = vkFeatures14.bresenhamLines,
                .smoothLines                            = vkFeatures14.smoothLines,
                .stippledRectangularLines               = vkFeatures14.stippledRectangularLines,
                .stippledBresenhamLines                 = vkFeatures14.stippledBresenhamLines,
                .stippledSmoothLines                    = vkFeatures14.stippledSmoothLines,
                .vertexAttributeInstanceRateDivisor     = vkFeatures14.vertexAttributeInstanceRateDivisor,
                .vertexAttributeInstanceRateZeroDivisor = vkFeatures14.vertexAttributeInstanceRateZeroDivisor,
                .indexTypeUint8                         = vkFeatures14.indexTypeUint8,
                .dynamicRenderingLocalRead              = VK_TRUE,   //Makes dynamic render passes behave like subpasses, this can increase performance, Especially on Mobile devices that use tile based rendering.
                .maintenance5                           = VK_TRUE,   // vkCmdBindIndexBuffer2 looks interesting for clustered rendering
                .maintenance6                           = vkFeatures14.maintenance6,
                .pipelineProtectedAccess                = vkFeatures14.pipelineProtectedAccess,
                .pipelineRobustness                     = vkFeatures14.pipelineRobustness,
                .hostImageCopy                          = vkFeatures14.hostImageCopy,
                .pushDescriptor                         = vkFeatures14.pushDescriptor,
            };

            CheckMissingDeviceFeatures(deviceFeatures10, startOfDeviceFeaturespNextChain, deviceFeatures11, vkFeatures11, deviceFeatures12, vkFeatures12, deviceFeatures13, vkFeatures13, deviceFeatures14, vkFeatures14);
            createInfoNext = &vkFeatures14;
        }
        else
        {
            CheckMissingDeviceFeatures(deviceFeatures10, startOfDeviceFeaturespNextChain, deviceFeatures11, vkFeatures11, deviceFeatures12, vkFeatures12, deviceFeatures13, vkFeatures13);
            createInfoNext = &vkFeatures13;
        }


        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .accelerationStructure                                  = VK_TRUE,
            .accelerationStructureCaptureReplay                     = VK_FALSE,
            .accelerationStructureIndirectBuild                     = VK_FALSE,
            .accelerationStructureHostCommands                      = VK_FALSE,
            .descriptorBindingAccelerationStructureUpdateAfterBind  = VK_TRUE,
        };

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            .rayTracingPipeline                                     = VK_TRUE,
            .rayTracingPipelineShaderGroupHandleCaptureReplay       = VK_FALSE,
            .rayTracingPipelineShaderGroupHandleCaptureReplayMixed  = VK_FALSE,
            .rayTracingPipelineTraceRaysIndirect                    = VK_TRUE,
            .rayTraversalPrimitiveCulling                           = VK_FALSE,
        };

        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            .rayQuery = VK_TRUE,
        };

        VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexTypeUint8Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
            .indexTypeUint8 = VK_TRUE,
        };



        std::vector<VkExtensionProperties> allDeviceExtensions;
        GetDeviceExtensions(physicalDevice, allDeviceExtensions);
        std::vector<const char*> deviceExtensionNames =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
            VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME,
        };


        auto addOptionalExtension = [&allDeviceExtensions, &deviceExtensionNames, &createInfoNext](const char* name, void* features = nullptr) mutable -> bool
        {
            bool extensionFound = false;
            for (const auto&[extensionName, specVersion] : allDeviceExtensions)
            {
                if (strcmp(extensionName, name) == 0) { extensionFound = true; }
            }
            if (!extensionFound)
            {
                EOS::Logger->warn("Device Extension: {} -> Disabled", name);
                return false;
            }

            deviceExtensionNames.emplace_back(name);
            if (features)
            {
                //Safe way to access both ->pNext members and write to it
                std::launder(static_cast<VkBaseOutStructure*>(features))->pNext = std::launder(static_cast<VkBaseOutStructure*>(createInfoNext));
                createInfoNext = features;

                EOS::Logger->debug("Device Extension: {} -> Enabled", name);
            }

            return true;
        };

        auto addOptionalExtensions = [&allDeviceExtensions, &deviceExtensionNames, &createInfoNext](const char* name1, const char* name2, void* features = nullptr) mutable -> bool
        {
            bool extension1Found = false;
            bool extension2Found = false;
            for (const auto&[extensionName, specVersion] : allDeviceExtensions)
            {
                if (strcmp(extensionName, name1) == 0) { extension1Found = true; }
                if (strcmp(extensionName, name2) == 0) { extension2Found = true; }
            }
            if (!extension1Found || !extension2Found)
            {
                EOS::Logger->warn("Device Extension: {} -> Disabled", name1);
                EOS::Logger->warn("Device Extension: {} -> Disabled", name2);
                return false;
            }

            deviceExtensionNames.push_back(name1);
            deviceExtensionNames.push_back(name2);
            if (features)
            {
                std::launder(static_cast<VkBaseOutStructure*>(features))->pNext =  std::launder(static_cast<VkBaseOutStructure*>(createInfoNext));
                createInfoNext = features;

                EOS::Logger->debug("Device Extension: {} -> Enabled", name1);
                EOS::Logger->debug("Device Extension: {} -> Enabled", name2);
            }

            return true;
        };

        addOptionalExtensions(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, &accelerationStructureFeatures);
        addOptionalExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rayTracingFeatures);
        addOptionalExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, &rayQueryFeatures);


        //TODO: Move Queue logic to separate function, be called from VulkanContext and pass needed info to this function. -> this doesn't really have anything to do with device creation
        deviceQueues.Graphics.QueueFamilyIndex = FindQueueFamilyIndex(physicalDevice, VK_QUEUE_GRAPHICS_BIT);
        if (deviceQueues.Graphics.QueueFamilyIndex == DeviceQueueIndex::InvalidIndex) { EOS::Logger->error("VK_QUEUE_GRAPHICS_BIT is not supported"); }

        deviceQueues.Compute.QueueFamilyIndex = FindQueueFamilyIndex(physicalDevice, VK_QUEUE_COMPUTE_BIT);
        if (deviceQueues.Compute.QueueFamilyIndex == DeviceQueueIndex::InvalidIndex) { EOS::Logger->error("VK_QUEUE_COMPUTE_BIT is not supported"); }

        constexpr float queuePriority = 1.0f;
        const VkDeviceQueueCreateInfo ciQueue[2] =
        {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = deviceQueues.Graphics.QueueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
                },
        {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = deviceQueues.Compute.QueueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
        };
        const uint32_t numQueues = ciQueue[0].queueFamilyIndex == ciQueue[1].queueFamilyIndex ? 1 : 2;

        const VkDeviceCreateInfo deviceCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &indexTypeUint8Features,
            .queueCreateInfoCount = numQueues,
            .pQueueCreateInfos = ciQueue,
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size()),
            .ppEnabledExtensionNames = deviceExtensionNames.data(),
            .pEnabledFeatures = &deviceFeatures10,
        };
        VK_ASSERT(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
        volkLoadDevice(device);

        VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_DEVICE, reinterpret_cast<uint64_t>(device), "Device: VulkanContext::Device"));
    }
};

namespace VkSwapChain
{
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
}

namespace VkSynchronization
{
    VkSemaphore CreateSemaphore(const VkDevice& device, const char* debugName)
    {
        constexpr VkSemaphoreCreateInfo createInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0,
        };

        VkSemaphore semaphore = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));
        VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(semaphore), debugName));

        return semaphore;
    }

    VkSemaphore CreateSemaphoreTimeline(const VkDevice &device, uint64_t initialValue, const char *debugName)
    {
        const VkSemaphoreTypeCreateInfo semaphoreTypeCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = initialValue,
        };

        const VkSemaphoreCreateInfo semaphoreCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &semaphoreTypeCreateInfo,
            .flags = 0,
        };

        VkSemaphore semaphore = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore));
        VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(semaphore), debugName));

        return semaphore;
    }

    VkFence CreateFence(const VkDevice &device, const char *debugName)
    {
        constexpr VkFenceCreateInfo createInfo =
        {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = 0,
        };

        VkFence fence = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateFence(device, &createInfo, nullptr, &fence));
        VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(fence), debugName));
        return fence;
    }

    VkPipelineStageFlags2 ConvertToVkPipelineStage2(const EOS::ResourceState &state)
    {
        VkPipelineStageFlags2 flags = 0;

        // Raytracing
        if (state & (EOS::ResourceState::AccelerationStructureRead))
        {
            flags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }

        if (state & (EOS::ResourceState::AccelerationStructureWrite))
        {
            flags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }

        // Graphics
        if (state & EOS::ResourceState::IndexBuffer)
        {
            flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        }

        if (state & EOS::ResourceState::VertexAndConstantBuffer)
        {
            flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            //TODO: Test this out if the latter flag is needed
            //flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        }

        if (state & EOS::ResourceState::PixelShaderResource)
        {
            flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        if (state & EOS::ResourceState::NonPixelShaderResource)
        {
            flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            //TODO: if (IsGeometryShaderSupported)
            {
                //flags |= VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT;
            }

            //TODO: if (IsTesselationSupported)
            {
                //flags |= VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT;
                //flags |= VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT;
            }

            //TODO: if (IsRayTracingSupported)
            {
                //flags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            }
        }

        if (state & EOS::ResourceState::UnorderedAccess)
        {
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }
        if (state & EOS::ResourceState::UnorderedAccessPixel)
        {
            flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        if (state & EOS::ResourceState::RenderTarget)
        {
            flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        if (state & (EOS::ResourceState::DepthRead | EOS::ResourceState::DepthWrite))
        {
            flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        }

        //TODO: Compute
        {
            //flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        //TODO: Transfer
        {
            //return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            //return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }

        // Compatible with both compute and graphics queues
        if (state & EOS::ResourceState::IndirectArgument)
        {
            flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        }

        if (state & (EOS::ResourceState::CopyDest | EOS::ResourceState::CopySource))
        {
            flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }

        if (flags == 0)
        {
            flags = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        }

        return flags;
    }

    VkAccessFlags2 ConvertToVkAccessFlags2(const EOS::ResourceState &state)
    {
        VkAccessFlags flags = 0;

        if (state & EOS::ResourceState::CopySource)
        {
            flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        if (state & EOS::ResourceState::CopyDest)
        {
            flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        if (state & EOS::ResourceState::VertexAndConstantBuffer)
        {
            flags |= VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        }

        if (state & EOS::ResourceState::IndexBuffer)
        {
            flags |= VK_ACCESS_2_INDEX_READ_BIT;
        }

        if (state & (EOS::ResourceState::UnorderedAccess | EOS::ResourceState::UnorderedAccessPixel))
        {
            flags |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        if (state & EOS::ResourceState::IndirectArgument)
        {
            flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }

        if (state & EOS::ResourceState::RenderTarget)
        {
            flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (state & EOS::ResourceState::DepthWrite)
        {
            flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        if (state & EOS::ResourceState::DepthRead)
        {
            flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }

        if (state & EOS::ResourceState::ShaderResource)
        {
            flags |= VK_ACCESS_2_SHADER_READ_BIT;
        }

        if (state & EOS::ResourceState::Present)
        {
            flags |= VK_ACCESS_2_MEMORY_READ_BIT;
        }

        if (state & EOS::ResourceState::AccelerationStructureRead)
        {
            flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        }

        if (state & EOS::ResourceState::AccelerationStructureWrite)
        {
            flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }

        return flags;
    }

    VkImageLayout ConvertToVkImageLayout(const EOS::ResourceState &state)
    {
        if (state & EOS::ResourceState::CopySource)
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        if (state & EOS::ResourceState::CopyDest)
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        if (state & EOS::ResourceState::RenderTarget)
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        if (state & EOS::ResourceState::DepthWrite)
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        if (state & EOS::DepthRead)
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        if (state & (EOS::ResourceState::UnorderedAccess | EOS::UnorderedAccessPixel))
            return VK_IMAGE_LAYOUT_GENERAL;

        if (state & EOS::ResourceState::ShaderResource)
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (state & EOS::ResourceState::Present)
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        if (state == EOS::ResourceState::Common)
            return VK_IMAGE_LAYOUT_GENERAL;

        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    VkImageAspectFlags ConvertToVkImageAspectFlags(const EOS::ResourceState &state)
    {
        VkImageAspectFlags aspectMask = 0;

        if (state & EOS::ResourceState::RenderTarget)
        {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        else
        {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        if (state & (EOS::ResourceState::DepthRead | EOS::ResourceState::DepthWrite))
        {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        return aspectMask;
    }
}
