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
            const auto vkDevice = reinterpret_cast<VkPhysicalDevice>(device.ID);
            DeviceScore scoreEntry{&device};

            //Check device type
            switch (device.Type)
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
                scoreEntry.score += static_cast<int>(props.apiVersion);

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
            physicalDevice = reinterpret_cast<VkPhysicalDevice>(bestDevice->desc->ID);

            EOS::Logger->info("Selected device: {} (Score: {})", bestDevice->desc->Name, bestDevice->score);
        } else
        {
            // Fallback to first device with warning
            physicalDevice = reinterpret_cast<VkPhysicalDevice>(hardwareDevices.back().ID);
            EOS::Logger->warn("No optimal device found! Using fallback: {}", hardwareDevices.back().Name);
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



        //Setup Device Queues
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

        //Get Features
        VkPhysicalDeviceVulkan14Features vkFeatures14       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext =  nullptr};
        VkPhysicalDeviceVulkan13Features vkFeatures13       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &vkFeatures14};
        VkPhysicalDeviceVulkan12Features vkFeatures12       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &vkFeatures13};
        VkPhysicalDeviceVulkan11Features vkFeatures11       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &vkFeatures12};
        VkPhysicalDeviceFeatures2 startOfDeviceFeaturespNextChain = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &vkFeatures11};

        vkGetPhysicalDeviceFeatures2(physicalDevice, &startOfDeviceFeaturespNextChain);

        //Setup the desired features then we check if our device supports these desired features.
        VkPhysicalDeviceFeatures deviceFeatures10
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

        VkPhysicalDeviceVulkan11Features deviceFeatures11
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

        //Will only be tried to enable on 1.3 Vulkan
        VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexTypeUint8Features =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
           .indexTypeUint8 = VK_TRUE,
        };

        void* createInfoNext = nullptr;
        VkPhysicalDeviceVulkan14Features deviceFeatures14{}; //TODO: Check if VkPhysicalDeviceVulkan14Features will work in 1.3
        if (version == vkVersion::VERSION_14)
        {
            deviceFeatures14 =
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
                .dynamicRenderingLocalRead              = vkFeatures14.dynamicRenderingLocalRead,   //Makes dynamic render passes behave like subpasses, this can increase performance, Especially on Mobile devices that use tile based rendering.
                .maintenance5                           = vkFeatures14.maintenance5,                // vkCmdBindIndexBuffer2 looks interesting for clustered rendering
                .maintenance6                           = vkFeatures14.maintenance6,
                .pipelineProtectedAccess                = vkFeatures14.pipelineProtectedAccess,
                .pipelineRobustness                     = vkFeatures14.pipelineRobustness,
                .hostImageCopy                          = vkFeatures14.hostImageCopy,
                .pushDescriptor                         = vkFeatures14.pushDescriptor,
            };
            CheckMissingDeviceFeatures(deviceFeatures10, startOfDeviceFeaturespNextChain, deviceFeatures11, vkFeatures11, deviceFeatures12, vkFeatures12, deviceFeatures13, vkFeatures13, deviceFeatures14, vkFeatures14);

            deviceFeatures14.pNext = &deviceFeatures13;
            createInfoNext = &deviceFeatures14;
        }
        else
        {
            CheckMissingDeviceFeatures(deviceFeatures10, startOfDeviceFeaturespNextChain, deviceFeatures11, vkFeatures11, deviceFeatures12, vkFeatures12, deviceFeatures13, vkFeatures13);
            createInfoNext = &deviceFeatures13;
        }

        std::vector<VkExtensionProperties> allDeviceExtensions;
        GetDeviceExtensions(physicalDevice, allDeviceExtensions);
        std::vector<const char*> deviceExtensionNames =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
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

        if (version == vkVersion::VERSION_13)
        {
            addOptionalExtension(VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME, &indexTypeUint8Features);
        }


        const VkDeviceCreateInfo deviceCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = createInfoNext,
            .queueCreateInfoCount = numQueues,
            .pQueueCreateInfos = ciQueue,
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size()),
            .ppEnabledExtensionNames = deviceExtensionNames.data(),
            .pEnabledFeatures = &deviceFeatures10,
        };

        VK_ASSERT(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
        volkLoadDevice(device);
        VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_DEVICE, reinterpret_cast<uint64_t>(device), "Device: VulkanContext::Device"));

        //Fill in our Device Queue's
        vkGetDeviceQueue(device, deviceQueues.Compute.QueueFamilyIndex, 0, &deviceQueues.Compute.Queue);
        vkGetDeviceQueue(device, deviceQueues.Graphics.QueueFamilyIndex, 0, &deviceQueues.Graphics.Queue);
    }

    EOS::Format vkFormatToFormat(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_UNDEFINED:
                return EOS::Format::Invalid;
            case VK_FORMAT_R8_UNORM:
                return EOS::Format::R_UN8;
            case VK_FORMAT_R16_UNORM:
                return EOS::Format::R_UN16;
            case VK_FORMAT_R16_SFLOAT:
                return EOS::Format::R_F16;
            case VK_FORMAT_R16_UINT:
                return EOS::Format::R_UI16;
            case VK_FORMAT_R8G8_UNORM:
                return EOS::Format::RG_UN8;
            case VK_FORMAT_B8G8R8A8_UNORM:
                return EOS::Format::BGRA_UN8;
            case VK_FORMAT_R8G8B8A8_UNORM:
                return EOS::Format::RGBA_UN8;
            case VK_FORMAT_R8G8B8A8_SRGB:
                return EOS::Format::RGBA_SRGB8;
            case VK_FORMAT_B8G8R8A8_SRGB:
                return EOS::Format::BGRA_SRGB8;
            case VK_FORMAT_R16G16_UNORM:
                return EOS::Format::RG_UN16;
            case VK_FORMAT_R16G16_SFLOAT:
                return EOS::Format::RG_F16;
            case VK_FORMAT_R32G32_SFLOAT:
                return EOS::Format::RG_F32;
            case VK_FORMAT_R16G16_UINT:
                return EOS::Format::RG_UI16;
            case VK_FORMAT_R32_SFLOAT:
                return EOS::Format::R_F32;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                return EOS::Format::RGBA_F16;
            case VK_FORMAT_R32G32B32A32_UINT:
                return EOS::Format::RGBA_UI32;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
                return EOS::Format::RGBA_F32;
            case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
                return EOS::Format::ETC2_RGB8;
            case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
                return EOS::Format::ETC2_SRGB8;
            case VK_FORMAT_D16_UNORM:
                return EOS::Format::Z_UN16;
            case VK_FORMAT_BC7_UNORM_BLOCK:
                return EOS::Format::BC7_RGBA;
            case VK_FORMAT_X8_D24_UNORM_PACK32:
                return EOS::Format::Z_UN24;
            case VK_FORMAT_D24_UNORM_S8_UINT:
                return EOS::Format::Z_UN24_S_UI8;
            case VK_FORMAT_D32_SFLOAT:
                return EOS::Format::Z_F32;
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return EOS::Format::Z_F32_S_UI8;
            case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
                return EOS::Format::YUV_NV12;
            case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
                return EOS::Format::YUV_420p;
            default:;
        }

        CHECK(false, "VkFormat value not handled: {}", static_cast<int>(format));
        return EOS::Format::Invalid;

    }

    VkFormat FormatTovkFormat(EOS::Format format)
    {
        switch (format)
        {
            case EOS::Format::Invalid:
                return VK_FORMAT_UNDEFINED;
            case EOS::Format::R_UN8:
                return VK_FORMAT_R8_UNORM;
            case EOS::Format::R_UN16:
                return VK_FORMAT_R16_UNORM;
            case EOS::Format::R_F16:
                return VK_FORMAT_R16_SFLOAT;
            case EOS::Format::R_UI16:
                return VK_FORMAT_R16_UINT;
            case EOS::Format::RG_UN8:
                return VK_FORMAT_R8G8_UNORM;
            case EOS::Format::BGRA_UN8:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case EOS::Format::RGBA_UN8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case EOS::Format::RGBA_SRGB8:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case EOS::Format::BGRA_SRGB8:
                return VK_FORMAT_B8G8R8A8_SRGB;
            case EOS::Format::RG_UN16:
                return VK_FORMAT_R16G16_UNORM;
            case EOS::Format::RG_F16:
                return VK_FORMAT_R16G16_SFLOAT;
            case EOS::Format::RG_F32:
                return VK_FORMAT_R32G32_SFLOAT;
            case EOS::Format::RG_UI16:
                return VK_FORMAT_R16G16_UINT;
            case EOS::Format::R_F32:
                return VK_FORMAT_R32_SFLOAT;
            case EOS::Format::RGBA_F16:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case EOS::Format::RGBA_UI32:
                return VK_FORMAT_R32G32B32A32_UINT;
            case EOS::Format::RGBA_F32:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case EOS::Format::ETC2_RGB8:
                return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
            case EOS::Format::ETC2_SRGB8:
                return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
            case EOS::Format::Z_UN16:
                return VK_FORMAT_D16_UNORM;
            case EOS::Format::BC7_RGBA:
                return VK_FORMAT_BC7_UNORM_BLOCK;
            case EOS::Format::Z_UN24:
                return VK_FORMAT_X8_D24_UNORM_PACK32;
            case EOS::Format::Z_UN24_S_UI8:
                return VK_FORMAT_D24_UNORM_S8_UINT;
            case EOS::Format::Z_F32:
                return  VK_FORMAT_D32_SFLOAT;
            case EOS::Format::Z_F32_S_UI8:
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            case EOS::Format::YUV_NV12:
                return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
            case EOS::Format::YUV_420p:
                return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
            default:;
        }

        CHECK(false, "VkFormat value not handled: {}", static_cast<int>(format));
        return VK_FORMAT_UNDEFINED;;
    }

    VkFormat VertexFormatToVkFormat(EOS::VertexFormat format)
    {
        using EOS::VertexFormat;
        switch (format)
        {
            case VertexFormat::Invalid:
              CHECK(false, "The Format is not valid");
              return VK_FORMAT_UNDEFINED;
            case VertexFormat::Float1:
              return VK_FORMAT_R32_SFLOAT;
            case VertexFormat::Float2:
              return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3:
              return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4:
              return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexFormat::Byte1:
              return VK_FORMAT_R8_SINT;
            case VertexFormat::Byte2:
              return VK_FORMAT_R8G8_SINT;
            case VertexFormat::Byte3:
              return VK_FORMAT_R8G8B8_SINT;
            case VertexFormat::Byte4:
              return VK_FORMAT_R8G8B8A8_SINT;
            case VertexFormat::UByte1:
              return VK_FORMAT_R8_UINT;
            case VertexFormat::UByte2:
              return VK_FORMAT_R8G8_UINT;
            case VertexFormat::UByte3:
              return VK_FORMAT_R8G8B8_UINT;
            case VertexFormat::UByte4:
              return VK_FORMAT_R8G8B8A8_UINT;
            case VertexFormat::Short1:
              return VK_FORMAT_R16_SINT;
            case VertexFormat::Short2:
              return VK_FORMAT_R16G16_SINT;
            case VertexFormat::Short3:
              return VK_FORMAT_R16G16B16_SINT;
            case VertexFormat::Short4:
              return VK_FORMAT_R16G16B16A16_SINT;
            case VertexFormat::UShort1:
              return VK_FORMAT_R16_UINT;
            case VertexFormat::UShort2:
              return VK_FORMAT_R16G16_UINT;
            case VertexFormat::UShort3:
              return VK_FORMAT_R16G16B16_UINT;
            case VertexFormat::UShort4:
              return VK_FORMAT_R16G16B16A16_UINT;
            case VertexFormat::Byte2Norm:
              return VK_FORMAT_R8G8_SNORM;
            case VertexFormat::Byte4Norm:
              return VK_FORMAT_R8G8B8A8_SNORM;
            case VertexFormat::UByte2Norm:
              return VK_FORMAT_R8G8_UNORM;
            case VertexFormat::UByte4Norm:
              return VK_FORMAT_R8G8B8A8_UNORM;
            case VertexFormat::Short2Norm:
              return VK_FORMAT_R16G16_SNORM;
            case VertexFormat::Short4Norm:
              return VK_FORMAT_R16G16B16A16_SNORM;
            case VertexFormat::UShort2Norm:
              return VK_FORMAT_R16G16_UNORM;
            case VertexFormat::UShort4Norm:
              return VK_FORMAT_R16G16B16A16_UNORM;
            case VertexFormat::Int1:
              return VK_FORMAT_R32_SINT;
            case VertexFormat::Int2:
              return VK_FORMAT_R32G32_SINT;
            case VertexFormat::Int3:
              return VK_FORMAT_R32G32B32_SINT;
            case VertexFormat::Int4:
              return VK_FORMAT_R32G32B32A32_SINT;
            case VertexFormat::UInt1:
              return VK_FORMAT_R32_UINT;
            case VertexFormat::UInt2:
              return VK_FORMAT_R32G32_UINT;
            case VertexFormat::UInt3:
              return VK_FORMAT_R32G32B32_UINT;
            case VertexFormat::UInt4:
              return VK_FORMAT_R32G32B32A32_UINT;
            case VertexFormat::HalfFloat1:
              return VK_FORMAT_R16_SFLOAT;
            case VertexFormat::HalfFloat2:
              return VK_FORMAT_R16G16_SFLOAT;
            case VertexFormat::HalfFloat3:
              return VK_FORMAT_R16G16B16_SFLOAT;
            case VertexFormat::HalfFloat4:
              return VK_FORMAT_R16G16B16A16_SFLOAT;
        }
        CHECK(false, "Could not determine the format");
        return VK_FORMAT_UNDEFINED;
    }

    VkBlendFactor BlendFactorToVkBlendFactor(EOS::BlendFactor value)
    {
        switch (value)
        {
            case EOS::BlendFactor::Zero:
                return VK_BLEND_FACTOR_ZERO;
            case EOS::BlendFactor::One:
                return VK_BLEND_FACTOR_ONE;
            case EOS::BlendFactor::SrcColor:
                return VK_BLEND_FACTOR_SRC_COLOR;
            case EOS::BlendFactor::OneMinusSrcColor:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case EOS::BlendFactor::DstColor:
                return VK_BLEND_FACTOR_DST_COLOR;
            case EOS::BlendFactor::OneMinusDstColor:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case EOS::BlendFactor::SrcAlpha:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case EOS::BlendFactor::OneMinusSrcAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case EOS::BlendFactor::DstAlpha:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case EOS::BlendFactor::OneMinusDstAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case EOS::BlendFactor::BlendColor:
                return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case EOS::BlendFactor::OneMinusBlendColor:
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case EOS::BlendFactor::BlendAlpha:
                return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            case EOS::BlendFactor::OneMinusBlendAlpha:
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            case EOS::BlendFactor::SrcAlphaSaturated:
                return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            case EOS::BlendFactor::Src1Color:
                return VK_BLEND_FACTOR_SRC1_COLOR;
            case EOS::BlendFactor::OneMinusSrc1Color:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            case EOS::BlendFactor::Src1Alpha:
                return VK_BLEND_FACTOR_SRC1_ALPHA;
            case EOS::BlendFactor::OneMinusSrc1Alpha:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            default:
                CHECK(false, "Could not find a proper VkBlendFactor");
                return VK_BLEND_FACTOR_ONE; // default for unsupported values
        }
    }

    VkBlendOp BlendOpToVkBlendOp(EOS::BlendOp value)
    {
        switch (value)
        {
            case EOS::BlendOp::Add:
                return VK_BLEND_OP_ADD;
            case EOS::BlendOp::Subtract:
                return VK_BLEND_OP_SUBTRACT;
            case EOS::BlendOp::ReverseSubtract:
                return VK_BLEND_OP_REVERSE_SUBTRACT;
            case EOS::BlendOp::Min:
                return VK_BLEND_OP_MIN;
            case EOS::BlendOp::Max:
                return VK_BLEND_OP_MAX;
        }

        CHECK(false, "Could not find proper VkBlendOp");
        return VK_BLEND_OP_ADD;
    }

    VkSpecializationInfo GetPipelineShaderStageSpecializationInfo(const EOS::SpecializationConstantDescription &specializationDescription, VkSpecializationMapEntry *outEntries)
    {
        const uint32_t numEntries = specializationDescription.GetNumberOfSpecializationConstants();
        if (outEntries)
        {
            for (uint32_t i{}; i != numEntries; ++i)
            {
                outEntries[i] = VkSpecializationMapEntry
                {
                    .constantID = specializationDescription.Entries[i].ID,
                    .offset = specializationDescription.Entries[i].Offset,
                    .size = specializationDescription.Entries[i].Size,
                };
            }
        }

        return VkSpecializationInfo
        {
            .mapEntryCount = numEntries,
            .pMapEntries = outEntries,
            .dataSize = specializationDescription.DataSize,
            .pData = specializationDescription.Data,
        };
    }

    VkPipelineShaderStageCreateInfo GetPipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
        VkShaderModule shaderModule, const char *entryPoint, const VkSpecializationInfo *specializationInfo)
    {
        return VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .flags = 0,
            .stage = stage,
            .module = shaderModule,
            .pName = entryPoint ? entryPoint : "main",
            .pSpecializationInfo = specializationInfo,
        };
    }

    VkStencilOp StencilOpToVkStencilOp(const EOS::StencilOp op)
    {
        switch (op)
        {
            case EOS::StencilOp::Keep:
                return VK_STENCIL_OP_KEEP;
            case EOS::StencilOp::Zero:
                return VK_STENCIL_OP_ZERO;
            case EOS::StencilOp::Replace:
                return VK_STENCIL_OP_REPLACE;
            case EOS::StencilOp::IncrementClamp:
                return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case EOS::StencilOp::DecrementClamp:
                return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case EOS::StencilOp::Invert:
                return VK_STENCIL_OP_INVERT;
            case EOS::StencilOp::IncrementWrap:
                return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case EOS::StencilOp::DecrementWrap:
                return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        }
        CHECK(false, "Could not fin right Stencil OP");
        return VK_STENCIL_OP_KEEP;
    }

    VkCompareOp CompareOpToVkCompareOp(const EOS::CompareOp func)
    {
        switch (func)
        {
            case EOS::CompareOp::Never:
                return VK_COMPARE_OP_NEVER;
            case EOS::CompareOp::Less:
                return VK_COMPARE_OP_LESS;
            case EOS::CompareOp::Equal:
                return VK_COMPARE_OP_EQUAL;
            case EOS::CompareOp::LessEqual:
                return VK_COMPARE_OP_LESS_OR_EQUAL;
            case EOS::CompareOp::Greater:
                return VK_COMPARE_OP_GREATER;
            case EOS::CompareOp::NotEqual:
                return VK_COMPARE_OP_NOT_EQUAL;
            case EOS::CompareOp::GreaterEqual:
                return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case EOS::CompareOp::AlwaysPass:
                return VK_COMPARE_OP_ALWAYS;
        }
        CHECK(false, "CompareFunction value not handled: {}", static_cast<int>(func));
        return VK_COMPARE_OP_ALWAYS;
    }

    VkPrimitiveTopology TopologyToVkPrimitiveTopology(EOS::Topology t)
    {
        switch (t)
        {
            case EOS::Topology::Point:
                return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case EOS::Topology::Line:
                return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case EOS::Topology::LineStrip:
                return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case EOS::Topology::Triangle:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case EOS::Topology::TriangleStrip:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case EOS::Topology::Patch:
                return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        }
        CHECK(false, "Implement Topology = {}", static_cast<uint32_t>(t));
        return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }

    VkPolygonMode PolygonModeToVkPolygonMode(EOS::PolygonMode mode)
    {
        switch (mode)
        {
            case EOS::PolygonMode::Fill:
                return VK_POLYGON_MODE_FILL;
            case EOS::PolygonMode::Line:
                return VK_POLYGON_MODE_LINE;
        }
        CHECK(false, "Implement a missing polygon fill mode");
        return VK_POLYGON_MODE_FILL;
    }

    VkCullModeFlags CullModeToVkCullMode(EOS::CullMode mode)
    {
        switch (mode)
        {
            case EOS::CullMode::None:
                return VK_CULL_MODE_NONE;
            case EOS::CullMode::Front:
                return VK_CULL_MODE_FRONT_BIT;
            case EOS::CullMode::Back:
                return VK_CULL_MODE_BACK_BIT;
        }
        CHECK(false, "Implement a missing cull mode");
        return VK_CULL_MODE_NONE;
    }

    VkFrontFace WindingModeToVkFrontFace(EOS::WindingMode mode)
    {
        switch (mode) {
            case EOS::WindingMode::CounterClockWise:
                return VK_FRONT_FACE_COUNTER_CLOCKWISE;
            case EOS::WindingMode::ClockWise:
                return VK_FRONT_FACE_CLOCKWISE;
        }

        CHECK(false, "Cannot determine Winding Mode");
        return VK_FRONT_FACE_CLOCKWISE;
    }

    VkSampleCountFlagBits GetVulkanSampleCountFlags(uint32_t numSamples, VkSampleCountFlags maxSamplesMask)
    {
        if (numSamples <= 1 || VK_SAMPLE_COUNT_2_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_1_BIT;
        }

        if (numSamples <= 2 || VK_SAMPLE_COUNT_4_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_2_BIT;
        }

        if (numSamples <= 4 || VK_SAMPLE_COUNT_8_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_4_BIT;
        }

        if (numSamples <= 8 || VK_SAMPLE_COUNT_16_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_8_BIT;
        }

        if (numSamples <= 16 || VK_SAMPLE_COUNT_32_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_16_BIT;
        }

        if (numSamples <= 32 || VK_SAMPLE_COUNT_64_BIT > maxSamplesMask)
        {
            return VK_SAMPLE_COUNT_32_BIT;
        }

        return VK_SAMPLE_COUNT_64_BIT;
    }

    uint32_t GetFramebufferMSAABitMask(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        return props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    }

    VkDescriptorSetLayoutBinding GetDSLBinding(uint32_t binding, VkDescriptorType descriptorType, uint32_t descriptorCount, VkShaderStageFlags stageFlags)
    {
        return VkDescriptorSetLayoutBinding
        {
            .binding = binding,
            .descriptorType = descriptorType,
            .descriptorCount = descriptorCount,
            .stageFlags = stageFlags,
            .pImmutableSamplers = nullptr,
        };
    }

    VkAttachmentLoadOp LoadOpToVkAttachmentLoadOp(EOS::LoadOp loadOp)
    {
        switch (loadOp)
        {
            case EOS::LoadOp::Invalid:
                CHECK(false, "LoadOp is in a Invalid state");
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            case EOS::LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            case EOS::LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case EOS::LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case EOS::LoadOp::None:
                return VK_ATTACHMENT_LOAD_OP_NONE_EXT;
        }

        CHECK(false, "Could not resolve LoadOp");
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    VkAttachmentStoreOp StoreOpToVkAttachmentStoreOp(EOS::StoreOp storeOp)
    {
        switch (storeOp)
        {
            case EOS::StoreOp::DontCare:
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            case EOS::StoreOp::Store:
                return VK_ATTACHMENT_STORE_OP_STORE;
            case EOS::StoreOp::MsaaResolve:
                // for MSAA resolve, we have to store data into a special "resolve" attachment
                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            case EOS::StoreOp::None:
                return VK_ATTACHMENT_STORE_OP_NONE;
        }

        CHECK(false, "Could not resolve StoreOp");
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    VkBufferUsageFlags BufferUsageFlagsToVkBufferUsageFlags(EOS::BufferUsageFlags bufferUsageFlags)
    {
        VkBufferUsageFlags usageFlags{};

        if (bufferUsageFlags & EOS::BufferUsageFlags::Index)
        {
            usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::Vertex)
        {
            usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::Uniform)
        {
            usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::Storage)
        {
            usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::Indirect)
        {
            usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::ShaderBindingTable)
        {
            usageFlags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::AccelStructBuildInputReadOnly)
        {
            usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        if (bufferUsageFlags & EOS::BufferUsageFlags::AccelStructStorage)
        {
            usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;
        }

        return usageFlags;
    }

    VkMemoryPropertyFlags StorageTypeToVkMemoryPropertyFlags(EOS::StorageType storage)
    {
        VkMemoryPropertyFlags memFlags{0};

        switch (storage)
        {
            case EOS::StorageType::Device:
                memFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                break;
            case EOS::StorageType::HostVisible:
                memFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                break;
            case EOS::StorageType::Memoryless:
                memFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
                break;
        }
        return memFlags;
    }
};

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
            //flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT; //TODO: Test this out if the latter flag is needed
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