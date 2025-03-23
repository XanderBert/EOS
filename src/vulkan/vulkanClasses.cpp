#include "vulkanClasses.h"
#include <string.h>


#define VOLK_IMPLEMENTATION
#include "vulkan/vkTools.h"

namespace EOS
{
    VulkanSwapChain::VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription)
    {
        SurfaceFormat = GetSwapChainFormat();
    }

    VulkanContext::VulkanContext(const ContextCreationDescription& contextDescription)
    : Configuration(contextDescription.config)
    {
        if (volkInitialize() != VK_SUCCESS) { exit(255); }
        CreateVulkanInstance();
        volkLoadInstance(VulkanInstance);
        SetupDebugMessenger();
        CreateSurface(contextDescription.window, contextDescription.display);

        //Select the Physical Device
        std::vector<HardwareDeviceDescription> hardwareDevices;
        GetHardwareDevice(contextDescription.preferredHardwareType, hardwareDevices);
        SelectHardwareDevice(hardwareDevices, VulkanPhysicalDevice);

        //Device Properties
        VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2;
        VkPhysicalDeviceDriverProperties vkPhysicalDeviceDriverProperties;
        GetPhysicalDeviceProperties(vkPhysicalDeviceProperties2, vkPhysicalDeviceDriverProperties, VulkanPhysicalDevice);
        const uint32_t driverAPIVersion = vkPhysicalDeviceProperties2.properties.apiVersion;
        printf("        Driver info: %s %s\n", vkPhysicalDeviceDriverProperties.driverName, vkPhysicalDeviceDriverProperties.driverInfo);
        printf("        Driver Vulkan API Version: %i.%i.%i\n", VK_API_VERSION_MAJOR(driverAPIVersion), VK_API_VERSION_MINOR(driverAPIVersion), VK_API_VERSION_PATCH(driverAPIVersion));

        //Device Features


        //TODO make 1.4 compatible
        //Get Features
        //VkPhysicalDeviceVulkan14Features vkFeatures14       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
        VkPhysicalDeviceVulkan13Features vkFeatures13       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = nullptr};
        VkPhysicalDeviceVulkan12Features vkFeatures12       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &vkFeatures13};
        VkPhysicalDeviceVulkan11Features vkFeatures11       = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &vkFeatures12};
        VkPhysicalDeviceFeatures2 startOfDeviceFeaturespNextChain = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &vkFeatures11};
        vkGetPhysicalDeviceFeatures2(VulkanPhysicalDevice, &startOfDeviceFeaturespNextChain);

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

        CheckMissingDeviceFeatures(deviceFeatures10, startOfDeviceFeaturespNextChain, deviceFeatures11, vkFeatures11, deviceFeatures12, vkFeatures12, deviceFeatures13, vkFeatures13);


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
        PrintDeviceExtensions(VulkanPhysicalDevice, allDeviceExtensions);
        std::vector<const char*> deviceExtensionNames =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
            VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME,
        };

        void* createInfoNext = &vkFeatures13;
        auto addOptionalExtension = [&allDeviceExtensions, &deviceExtensionNames, &createInfoNext](const char* name, void* features = nullptr) mutable -> bool
        {
            bool extensionFound = false;
            for (const auto& extension : allDeviceExtensions)
            {
                if (strcmp(extension.extensionName, name) == 0) { extensionFound = true; }
            }
            if (!extensionFound)
            {
                printf("Device Extension:%s -> Disabled\n", name);
                return false;
            }

            deviceExtensionNames.emplace_back(name);
            if (features)
            {
                //Safe way to access both ->pNext members and write to it
                std::launder(static_cast<VkBaseOutStructure*>(features))->pNext = std::launder(static_cast<VkBaseOutStructure*>(createInfoNext));
                createInfoNext = features;

                printf("Device Extension:%s -> Enabled\n", name);
            }

            return true;
        };

        auto addOptionalExtensions = [&allDeviceExtensions, &deviceExtensionNames, &createInfoNext](const char* name1, const char* name2, void* features = nullptr) mutable -> bool
        {
            bool extension1Found = false;
            bool extension2Found = false;
            for (const auto& extension : allDeviceExtensions)
            {
                if (strcmp(extension.extensionName, name1) == 0) { extension1Found = true; }
                if (strcmp(extension.extensionName, name2) == 0) { extension2Found = true; }
            }
            if (!extension1Found || !extension2Found)
            {
                printf("Device Extension:%s -> Disabled\n", name1);
                printf("Device Extension:%s -> Disabled\n", name2);
                return false;
            }

            deviceExtensionNames.push_back(name1);
            deviceExtensionNames.push_back(name2);
            if (features)
            {
                std::launder(static_cast<VkBaseOutStructure*>(features))->pNext =  std::launder(static_cast<VkBaseOutStructure*>(createInfoNext));
                createInfoNext = features;
                printf("Device Extension:%s -> Enabled\n", name1);
                printf("Device Extension:%s -> Enabled\n", name2);
            }

            return true;
        };

        addOptionalExtensions(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, &accelerationStructureFeatures);
        addOptionalExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rayTracingFeatures);
        addOptionalExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, &rayQueryFeatures);

        VulkanDeviceQueues.Graphics.QueueFamilyIndex = FindQueueFamilyIndex(VulkanPhysicalDevice, VK_QUEUE_GRAPHICS_BIT);
        if (VulkanDeviceQueues.Graphics.QueueFamilyIndex == DeviceQueues::InvalidIndex) { printf("VK_QUEUE_GRAPHICS_BIT is not supported"); }
        VulkanDeviceQueues.Compute.QueueFamilyIndex = FindQueueFamilyIndex(VulkanPhysicalDevice, VK_QUEUE_COMPUTE_BIT);
        if (VulkanDeviceQueues.Compute.QueueFamilyIndex == DeviceQueues::InvalidIndex) { printf("VK_QUEUE_COMPUTE_BIT is not supported"); }

        constexpr float queuePriority = 1.0f;
        const VkDeviceQueueCreateInfo ciQueue[2] =
        {
            {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = VulkanDeviceQueues.Graphics.QueueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
        {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = VulkanDeviceQueues.Compute.QueueFamilyIndex,
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
        VK_ASSERT(vkCreateDevice(VulkanPhysicalDevice, &deviceCreateInfo, nullptr, &VulkanDevice));
        volkLoadDevice(VulkanDevice);


        //Create Swapchain
    }

    void VulkanContext::CreateVulkanInstance()
    {
        uint32_t apiVersion{};
        vkEnumerateInstanceVersion(&apiVersion);
        printf("Vulkan Instance API Version: %d.%d.%d \n", VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));
        const VkApplicationInfo applicationInfo
        {
            .pApplicationName   = "EOS", //TODO Change this
            .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
            .pEngineName        = "EOS",
            .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
            .apiVersion         = apiVersion,
        };

        //Check if we can use Validation Layers
        uint32_t numberOfLayers{};
        vkEnumerateInstanceLayerProperties(&numberOfLayers, nullptr);
        std::vector<VkLayerProperties> layerProperties(numberOfLayers);
        vkEnumerateInstanceLayerProperties(&numberOfLayers, layerProperties.data());
        bool foundLayer = false;
        for (const VkLayerProperties& props : layerProperties)
        {
            //TODO: This will always be true
            if (strcmp(props.layerName, validationLayer) == 0)
            {
                foundLayer = true;
                break;
            }
        }
        Configuration.enableValidationLayers = foundLayer;

        //Setup the validation layers and extensions
        uint32_t instanceExtensionCount;
        VK_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr));
        std::vector<VkExtensionProperties> allInstanceExtensions(instanceExtensionCount);
        VK_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, allInstanceExtensions.data()));

        std::vector<VkValidationFeatureEnableEXT> validationFeatures;
        validationFeatures.reserve(2);

        std::vector<const char*> instanceExtensionNames;
        std::vector<const char*> availableInstanceExtensionNames;
        instanceExtensionNames.reserve(4);
        availableInstanceExtensionNames.reserve(5);

        if (Configuration.enableValidationLayers)
        {
            validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            instanceExtensionNames.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        instanceExtensionNames.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);

        //Choose the right surface extension
        #if defined(EOS_PLATFORM_WINDOWS)
                instanceExtensionNames.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        #elif defined(EOS_PLATFORM_WAYLAND)
                instanceExtensionNames.emplace_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        #elif defined(EOS_PLATFORM_X11)
                instanceExtensionNames.emplace_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        #endif


        // log and check available instance extensions
        printf("\nAvailable Vulkan instance extensions:\n");
        for (const VkExtensionProperties& extension : allInstanceExtensions)
        {
            printf("    %s\n", extension.extensionName);
        }

        bool foundInstanceExtension = false;
        for (const auto& instanceExtensionName : instanceExtensionNames)
        {
            foundInstanceExtension = false;
            for (const VkExtensionProperties& extension : allInstanceExtensions)
            {
                if (strcmp(extension.extensionName, instanceExtensionName) == 0)
                {
                    foundInstanceExtension = true;
                    availableInstanceExtensionNames.emplace_back(instanceExtensionName);
                    break;
                }
            }

            if (!foundInstanceExtension)
            {
                printf("    %s -> Is not available on your device.\n", instanceExtensionName);
            }
        }

        VkValidationFeaturesEXT features =
        {
            .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext                          = nullptr,
            .enabledValidationFeatureCount  = static_cast<uint32_t>(validationFeatures.size()),
            .pEnabledValidationFeatures     = validationFeatures.data()
        };

        VkBool32 fine_grained_locking{VK_TRUE};
        VkBool32 validate_core{VK_TRUE};
        VkBool32 check_image_layout{VK_TRUE};
        VkBool32 check_command_buffer{VK_TRUE};
        VkBool32 check_object_in_use{VK_TRUE};
        VkBool32 check_query{VK_TRUE};
        VkBool32 check_shaders{VK_TRUE};
        VkBool32 check_shaders_caching{VK_TRUE};
        VkBool32 unique_handles{VK_TRUE};
        VkBool32 object_lifetime{VK_TRUE};
        VkBool32 stateless_param{VK_TRUE};
        std::vector<const char*> debug_action{"VK_DBG_LAYER_ACTION_LOG_MSG"};  // "VK_DBG_LAYER_ACTION_DEBUG_OUTPUT", "VK_DBG_LAYER_ACTION_BREAK"
        std::vector<const char*> report_flags{"error"};
        std::vector<VkLayerSettingEXT> layerSettings
        {
                {validationLayer, "fine_grained_locking", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &fine_grained_locking},
                {validationLayer, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &validate_core},
                {validationLayer, "check_image_layout", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_image_layout},
                {validationLayer, "check_command_buffer", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_command_buffer},
                {validationLayer, "check_object_in_use", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_object_in_use},
                {validationLayer, "check_query", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_query},
                {validationLayer, "check_shaders", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_shaders},
                {validationLayer, "check_shaders_caching", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_shaders_caching},
                {validationLayer, "unique_handles", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &unique_handles},
                {validationLayer, "object_lifetime", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &object_lifetime},
                {validationLayer, "stateless_param", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &stateless_param},
                {validationLayer, "debug_action", VK_LAYER_SETTING_TYPE_STRING_EXT, static_cast<uint32_t>(debug_action.size()), debug_action.data()},
                {validationLayer, "report_flags", VK_LAYER_SETTING_TYPE_STRING_EXT, static_cast<uint32_t>(report_flags.size()), report_flags.data()},
        };

        VkLayerSettingsCreateInfoEXT layerSettingsCreateInfo =
        {
            .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            .pNext        = &features,
            .settingCount = static_cast<uint32_t>(layerSettings.size()),
            .pSettings    = layerSettings.data(),
        };

        constexpr VkInstanceCreateFlags flags = 0;
        const VkInstanceCreateInfo instanceCreateInfo
        {
            .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext                   = &layerSettingsCreateInfo,
            .flags                   = flags,
            .pApplicationInfo        = &applicationInfo,
            .enabledLayerCount       = 1,
            .ppEnabledLayerNames     = &validationLayer,
            .enabledExtensionCount   = static_cast<uint32_t>(availableInstanceExtensionNames.size()),
            .ppEnabledExtensionNames = availableInstanceExtensionNames.data(),
        };

        // Actual Vulkan instance creation
        VK_ASSERT(vkCreateInstance(&instanceCreateInfo, nullptr, &VulkanInstance));
    }

    void VulkanContext::SetupDebugMessenger()
    {
       const VkDebugUtilsMessengerCreateInfoEXT debugUtilMessengerCreateInfo =
        {
           .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
           .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
           .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
           .pfnUserCallback = &DebugCallback,
           .pUserData = this,
       };

       VK_ASSERT(vkCreateDebugUtilsMessengerEXT(VulkanInstance, &debugUtilMessengerCreateInfo, nullptr, &VulkanDebugMessenger));
    }

    void VulkanContext::CreateSurface(void* window, [[maybe_unused]] void* display)
    {
#if defined(EOS_PLATFORM_WINDOWS)
        const VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = GetModuleHandle(nullptr),
            .hwnd = static_cast<HWND>(window),
        };
        //VK_ASSERT(vkCreateWin32SurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#elif defined(EOS_PLATFORM_X11)
        const VkXlibSurfaceCreateInfoKHR SurfaceCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .flags = 0,
            .dpy = (Display*)display,
            .window = (Window)window,
        };
        VK_ASSERT(vkCreateXlibSurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#elif defined(EOS_PLATFORM_WAYLAND)
        const VkWaylandSurfaceCreateInfoKHR SurfaceCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .flags = 0,
            .display = (wl_display*)display,
            .surface = (wl_surface*)window,
        };
        //VK_ASSERT(vkCreateWaylandSurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#endif
    }

    void VulkanContext::GetHardwareDevice(HardwareDeviceType desiredDeviceType, std::vector<HardwareDeviceDescription>& compatibleDevices) const
    {
        uint32_t deviceCount = 0;
        VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, nullptr));
        std::vector<VkPhysicalDevice> hardwareDevices(deviceCount);
        VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, hardwareDevices.data()));

        for (VkPhysicalDevice& hardwareDevice : hardwareDevices)
        {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(hardwareDevice, &deviceProperties);
            const auto deviceType = static_cast<HardwareDeviceType>(deviceProperties.deviceType);

            if (desiredDeviceType != HardwareDeviceType::Software && desiredDeviceType != deviceType) { continue; }

            //Convert the device to a unsigned (long) int (size dependant on building for 32 or 64 bit) and use that as the GUID of the physical device.
            compatibleDevices.emplace_back(reinterpret_cast<uintptr_t>(hardwareDevice), deviceType, deviceProperties.deviceName);
        }

        if (hardwareDevices.empty())
        {
            printf("Couldn't Find a physical hardware device");
            assert(false);
        }
    }

    bool VulkanContext::IsHostVisibleMemorySingleHeap() const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(VulkanPhysicalDevice, &memoryProperties);

        if (memoryProperties.memoryHeapCount != 1) { return false; }

        constexpr uint32_t checkFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        for (const auto& memoryType : memoryProperties.memoryTypes)
        {
            if ((memoryType.propertyFlags & checkFlags) == checkFlags) { return true; }
        }

        return false;
    }


}