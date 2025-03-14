#include "vulkanClasses.h"
#define VOLK_IMPLEMENTATION
#include "vulkan/vkTools.h"

namespace EOS
{
    VulkanContext::VulkanContext(const ContextCreationDescription& contextDescription)
    : Configuration(contextDescription.config)
    {
        if (volkInitialize() != VK_SUCCESS) { exit(255); }
        CreateVulkanInstance();
        volkLoadInstance(VulkanInstance);
        SetupDebugMessenger();

        std::vector<HardwareDeviceDescription> hardwareDevices;
        GetHardwareDevice(contextDescription.preferredHardwareType, hardwareDevices);
        if (hardwareDevices.empty())
        {
            printf("Couldn't Find a physical hardware device");
            assert(false);
        }

        VulkanPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(hardwareDevices.back().id);

        //Some GPU's have a shared memory
        bool useStagingBuffer = !IsHostVisibleMemorySingleHeap();

        //after device creation:
        //volkLoadDevice();
    }

    void VulkanContext::CreateVulkanInstance()
    {
        uint32_t apiVersion{};
        vkEnumerateInstanceVersion(&apiVersion);
        printf("Vulkan API Verion: %d.%d.%d \n", VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));
        const VkApplicationInfo applicationInfo
        {
            .pApplicationName   = "EOS", //TODO Change this
            .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
            .pEngineName        = "EOS",
            .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
            .apiVersion         = apiVersion,
        };

        //Check if we can use Validation Layers
        static constexpr const char* validationLayer {"VK_LAYER_KHRONOS_validation"};

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
        availableInstanceExtensionNames.reserve(4);

        if (Configuration.enableValidationLayers)
        {
            validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
            validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            instanceExtensionNames.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }

        instanceExtensionNames.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExtensionNames.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        //Choose the right surface extension
        #if defined(EOS_PLATFORM_WINDOWS)
                instanceExtensionNames.emplace_back("VK_KHR_win32_surface");
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
            const HardwareDeviceType deviceType = static_cast<HardwareDeviceType>(deviceProperties.deviceType);

            if (desiredDeviceType != HardwareDeviceType::Software && desiredDeviceType != deviceType) { continue; }

            //Convert the device to a unsigned (long) int (size dependant on building for 32 or 64 bit) and use that as the GUID of the physical device.
            compatibleDevices.emplace_back(reinterpret_cast<uintptr_t>(hardwareDevice), deviceType, deviceProperties.deviceName);
        }

        printf("Selected Hardware Device: %s \n", compatibleDevices.back().name.c_str());
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
