#include "vulkanClasses.h"
#include <string.h>
#include <ranges>

#include "vulkan/vkTools.h"


VulkanImage::VulkanImage(const ImageDescription &description)
: Image(description.Image)
, UsageFlags(description.UsageFlags)
, Extent(description.Extent)
, ImageType(description.ImageType)
, ImageFormat(description.ImageFormat)
{
    //TODO: Set Levels and Layers
    VK_ASSERT(VkDebug::SetDebugObjectName(description.Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(Image), description.DebugName));
    CreateImageView(ImageView, description.Device, Image, ImageType, ImageFormat, Levels, Layers, description.DebugName);
}

VkImageType VulkanImage::ToImageType(const EOS::ImageType imageType)
{
    switch (imageType)
    {
        case (EOS::ImageType::Image_1D):
        case (EOS::ImageType::Image_1D_Array):
            return VK_IMAGE_TYPE_1D;

        case(EOS::ImageType::Image_2D):
        case(EOS::ImageType::Image_2D_Array):
        case(EOS::ImageType::CubeMap):
        case(EOS::ImageType::CubeMap_Array):
        case(EOS::ImageType::SwapChain):
            return VK_IMAGE_TYPE_2D;

        case(EOS::ImageType::Image_3D):
            return  VK_IMAGE_TYPE_3D;
    }

    return VK_IMAGE_TYPE_MAX_ENUM;
}

VkImageViewType VulkanImage::ToImageViewType(EOS::ImageType imageType)
{
    switch (imageType)
    {
        case (EOS::ImageType::Image_1D):
            return VK_IMAGE_VIEW_TYPE_1D;
        case (EOS::ImageType::Image_1D_Array):
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case(EOS::ImageType::Image_2D):
            return VK_IMAGE_VIEW_TYPE_2D;
        case(EOS::ImageType::Image_2D_Array):
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case(EOS::ImageType::CubeMap):
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case(EOS::ImageType::CubeMap_Array):
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        case(EOS::ImageType::SwapChain):
            return VK_IMAGE_VIEW_TYPE_2D;
        case(EOS::ImageType::Image_3D):
            return  VK_IMAGE_VIEW_TYPE_3D;
    }

    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

void VulkanImage::CreateImageView(VkImageView& imageView, VkDevice device, VkImage image, const EOS::ImageType imageType,
    const VkFormat &imageFormat, const uint32_t levels, const uint32_t layers, const char *debugName)
{
    const VkImageViewCreateInfo createInfo =
   {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = ToImageViewType(imageType),
        .format = imageFormat,
        .components =  {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels , 0, layers},
    };

    VK_ASSERT(vkCreateImageView(device, &createInfo, nullptr, &imageView));
    VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView), debugName));
}

VulkanSwapChain::VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription)
: VkContext(vulkanSwapChainDescription.vulkanContext)
, GraphicsQueue(vulkanSwapChainDescription.vulkanContext->VulkanDeviceQueues.Graphics.Queue)
{
    //Get details of what we support
    const VulkanSwapChainSupportDetails supportDetails{*VkContext};

    //Get the Surface Format (format and color space)
    SurfaceFormat = VkSwapChain::GetSwapChainFormat(supportDetails.formats, VkContext->Configuration.DesiredSwapChainColorSpace);
    VkPresentModeKHR presentMode {VK_PRESENT_MODE_FIFO_KHR};

    // Try using Immediate mode presenting if we are running on a linux machine
    // For Windows we try to use Mailbox mode
    // If they are not available we use FIFO
#if defined(EOS_PLATFORM_WAYLAND) || defined(EOS_PLATFORM_X11)
    if (std::ranges::find(supportDetails.presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR) != supportDetails.presentModes.cend())
    {
        presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
#elif defined(EOS_PLATFORM_WIN32)
    if (std::ranges::find(supportDetails.presentModes, VK_PRESENT_MODE_MAILBOX_KHR) != supportDetails.presentModes.cend())
    {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
#endif

    // Check The surface and QueueFamilyIndex
    VkBool32 queueFamilySupportsPresentation = VK_FALSE;
    VK_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(VkContext->VulkanPhysicalDevice, VkContext->VulkanDeviceQueues.Graphics.QueueFamilyIndex, VkContext->VulkanSurface, &queueFamilySupportsPresentation));
    CHECK(queueFamilySupportsPresentation == VK_TRUE, "The queue family does not support presentation");

    //Get the device format properties
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(VkContext->VulkanPhysicalDevice, SurfaceFormat.format, &properties);

    // Get the imageUsageFlags
    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const bool isStorageSupported = (supportDetails.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) > 0;
    const bool isTilingOptimalSupported = (properties.optimalTilingFeatures & VK_IMAGE_USAGE_STORAGE_BIT > 0);

    if (isStorageSupported && isTilingOptimalSupported) { usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT; }

    const bool isCompositeAlphaSupported = (supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0;

    //Create SwapChain
    const VkSwapchainCreateInfoKHR createInfo
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = VkContext->VulkanSurface,
        .minImageCount = supportDetails.capabilities.minImageCount,
        .imageFormat = SurfaceFormat.format,
        .imageColorSpace = SurfaceFormat.colorSpace,
        .imageExtent = {.width = vulkanSwapChainDescription.width, .height = vulkanSwapChainDescription.height},
        .imageArrayLayers = 1,
        .imageUsage = usageFlags,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &VkContext->VulkanDeviceQueues.Graphics.QueueFamilyIndex,
        .preTransform = supportDetails.capabilities.currentTransform,
        .compositeAlpha = isCompositeAlphaSupported ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    VK_ASSERT(vkCreateSwapchainKHR(VkContext->VulkanDevice, &createInfo, nullptr, &SwapChain);)

    //Create SwapChain Images
    VK_ASSERT(vkGetSwapchainImagesKHR(VkContext->VulkanDevice,SwapChain, &NumberOfSwapChainImages, nullptr);)
    NumberOfSwapChainImages = std::min(NumberOfSwapChainImages, MAX_IMAGES);  //Make sure we don't go over our max defined SwapChain images
    std::vector<VkImage> swapChainImages(NumberOfSwapChainImages);
    VK_ASSERT(vkGetSwapchainImagesKHR(VkContext->VulkanDevice, SwapChain, &NumberOfSwapChainImages,swapChainImages.data()));
    CHECK(NumberOfSwapChainImages >  0, "Number of SwapChain images is 0");
    CHECK(!swapChainImages.empty(), "The SwapChain images didn't got created");

    AcquireSemaphores.clear();
    AcquireSemaphores.reserve(NumberOfSwapChainImages);

    Textures.clear();
    Textures.reserve(NumberOfSwapChainImages);

    TimelineWaitValues.resize(NumberOfSwapChainImages);

    ImageDescription swapChainImageDescription
    {
        .Image = {},
        .UsageFlags = usageFlags,
        .Extent = VkExtent3D{.width = vulkanSwapChainDescription.width, .height = vulkanSwapChainDescription.height, .depth = 1},
        .ImageType = EOS::ImageType::SwapChain,
        .ImageFormat = SurfaceFormat.format,
        .Device = VkContext->VulkanDevice,
    };

    // create images, image views and framebuffers
    for (uint32_t i{}; i < NumberOfSwapChainImages; ++i)
    {
        //Create our Acquire Semaphore for this swapChain image
        AcquireSemaphores.emplace_back(VkSynchronization::CreateSemaphore(vulkanSwapChainDescription.vulkanContext->VulkanDevice, "SwapChain Acquire Semaphore: " + i));

        //Create a image
        swapChainImageDescription.Image = swapChainImages[i];
        swapChainImageDescription.DebugName = "SwapChain Image: " + i;
        VulkanImage swapChainImage{swapChainImageDescription};

        Textures.emplace_back(vulkanSwapChainDescription.vulkanContext->TexturePool.Create(std::move(swapChainImage)));
    }
}

VulkanSwapChain::~VulkanSwapChain()
{
    for (EOS::TextureHandle& handle : Textures)
    {
        //TODO: Implement
        //VkContext->Destroy(handle);
    }

    vkDestroySwapchainKHR(VkContext->VulkanDevice, SwapChain, nullptr);
    for (const VkSemaphore semaphore : AcquireSemaphores)
    {
        vkDestroySemaphore(VkContext->VulkanDevice, semaphore, nullptr);
    }
}

void VulkanSwapChain::Present(VkSemaphore waitSemaphore)
{
    const VkPresentInfoKHR presentInfo =
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &SwapChain,
        .pImageIndices = &CurrentImageIndex,
    };

    const VkResult result = vkQueuePresentKHR(GraphicsQueue, &presentInfo);
    CHECK(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR, "Couldn't present the SwapChain image");

    GetNextImage = true;
    ++CurrentFrame;
}

VkImage VulkanSwapChain::GetCurrentImage() const
{
    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");
    return VkContext->TexturePool.Get(Textures[CurrentImageIndex])->Image;
}

VkImageView VulkanSwapChain::GetCurrentImageView() const
{
    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");
    return VkContext->TexturePool.Get(Textures[CurrentImageIndex])->ImageView;
}

uint32_t VulkanSwapChain::GetNumSwapChainImages() const
{
    return NumberOfSwapChainImages;
}

const VkSurfaceFormatKHR& VulkanSwapChain::GetFormat() const
{
    return SurfaceFormat;
}

EOS::TextureHandle VulkanSwapChain::GetCurrentTexture()
{
    GetAndWaitOnNextImage();

    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");

    //Returns a copy of the handle
    return Textures[CurrentImageIndex];
}

void VulkanSwapChain::GetAndWaitOnNextImage()
{
    //Get The Next SwapChain Image
    if (GetNextImage)
    {
        const VkSemaphoreWaitInfo waitInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &VkContext->TimelineSemaphore,
            .pValues = &TimelineWaitValues[CurrentImageIndex],
        };

        // when timeout is set to UINT64_MAX, we wait until the next image has been acquired
        VK_ASSERT(vkWaitSemaphores(VkContext->VulkanDevice, &waitInfo, UINT64_MAX));

        const VkSemaphore acquireSemaphore = AcquireSemaphores[CurrentImageIndex];
        const VkResult result = vkAcquireNextImageKHR(VkContext->VulkanDevice, SwapChain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &CurrentImageIndex);
        CHECK(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR, "vkAcquireNextImageKHR Failed");

        GetNextImage = false;
        VkContext->Commands->WaitSemaphore(acquireSemaphore);
    }
}

VulkanSwapChainSupportDetails::VulkanSwapChainSupportDetails(const VulkanContext& vulkanContext)
{
    //Get Surface Capabilities
    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &capabilities));

    //Get the Surface Support
    VK_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanDeviceQueues.Graphics.QueueFamilyIndex, vulkanContext.VulkanSurface, &queueFamilySupportsPresentation));

    //Get the available Surface Formats
    uint32_t formatCount;
    VK_ASSERT(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &formatCount, nullptr));
    formats.resize(formatCount);
    VK_ASSERT(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &formatCount, formats.data()));

    //Get the available Present Modes
    uint32_t presentModeCount;
    VK_ASSERT(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &presentModeCount, nullptr));
    presentModes.resize(presentModeCount);
    VK_ASSERT(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &presentModeCount, presentModes.data()));
}

void VulkanCommands::WaitSemaphore(const VkSemaphore &semaphore)
{
    CHECK(WaitOnSemaphore.semaphore == VK_NULL_HANDLE, "The wait Semaphore is not Empty");
    WaitOnSemaphore.semaphore = semaphore;
}

VulkanContext::VulkanContext(const EOS::ContextCreationDescription& contextDescription)
: Configuration(contextDescription.config)
{
    CHECK(volkInitialize() == VK_SUCCESS, "Failed to Initialize VOLK");

    CreateVulkanInstance(contextDescription.applicationName);
    SetupDebugMessenger();
    CreateSurface(contextDescription.window, contextDescription.display);

    //Select the Physical Device
    std::vector<EOS::HardwareDeviceDescription> hardwareDevices;
    GetHardwareDevice(contextDescription.preferredHardwareType, hardwareDevices);
    VkContext::SelectHardwareDevice(hardwareDevices, VulkanPhysicalDevice);

    VkContext::CreateVulkanDevice(VulkanDevice, VulkanPhysicalDevice, VulkanDeviceQueues);



    //Create SwapChain
    //TODO: will it need a description struct?
    VulkanSwapChainCreationDescription desc
    {
        .vulkanContext = this,
        .width = 100,
        .height = 80,
    };

    SwapChain = std::make_unique<VulkanSwapChain>(desc);
    TimelineSemaphore = VkSynchronization::CreateSemaphoreTimeline(VulkanDevice, SwapChain->GetNumSwapChainImages() - 1, "Semaphore: TimelineSemaphore");
}

void VulkanContext::CreateVulkanInstance(const char* applicationName)
{
    uint32_t apiVersion{};
    vkEnumerateInstanceVersion(&apiVersion);
    const VkApplicationInfo applicationInfo
    {
        .pApplicationName   = applicationName,
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

    EOS::Logger->debug("Vulkan Instance Extensions:\n     {}", fmt::join(allInstanceExtensions | std::views::transform([](const auto& ext) { return ext.extensionName; }), "\n     "));

    bool foundInstanceExtension = false;
    for (const auto& instanceExtensionName : instanceExtensionNames)
    {
        foundInstanceExtension = false;
        for (const auto&[extensionName, specVersion] : allInstanceExtensions)
        {
            if (strcmp(extensionName, instanceExtensionName) == 0)
            {
                foundInstanceExtension = true;
                availableInstanceExtensionNames.emplace_back(instanceExtensionName);
                break;
            }
        }

        if (!foundInstanceExtension)
        {
            EOS::Logger->warn("{} -> Is not available on your device.", instanceExtensionName);
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

    //Load the volk Instance
    volkLoadInstance(VulkanInstance);
}

void VulkanContext::SetupDebugMessenger()
{
   const VkDebugUtilsMessengerCreateInfoEXT debugUtilMessengerCreateInfo =
    {
       .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
       .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
       .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
       .pfnUserCallback = &VkDebug::DebugCallback,
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
    VK_ASSERT(vkCreateWin32SurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
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
        .display = static_cast<wl_display*>(display),
        .surface = static_cast<wl_surface*>(window),
    };
    VK_ASSERT(vkCreateWaylandSurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#endif
}

void VulkanContext::GetHardwareDevice(EOS::HardwareDeviceType desiredDeviceType, std::vector<EOS::HardwareDeviceDescription>& compatibleDevices) const
{
    uint32_t deviceCount = 0;
    VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> hardwareDevices(deviceCount);
    VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, hardwareDevices.data()));

    for (VkPhysicalDevice& hardwareDevice : hardwareDevices)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(hardwareDevice, &deviceProperties);
        const auto deviceType = static_cast<EOS::HardwareDeviceType>(deviceProperties.deviceType);

        if (desiredDeviceType != EOS::HardwareDeviceType::Software && desiredDeviceType != deviceType) { continue; }

        //Convert the device to a unsigned (long) int (size dependant on building for 32 or 64 bit) and use that as the GUID of the physical device.
        compatibleDevices.emplace_back(reinterpret_cast<uintptr_t>(hardwareDevice), deviceType, deviceProperties.deviceName);
    }

    CHECK(!hardwareDevices.empty(), "Couldn't find a physical hardware device!");
}

bool VulkanContext::IsHostVisibleMemorySingleHeap() const
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(VulkanPhysicalDevice, &memoryProperties);

    if (memoryProperties.memoryHeapCount != 1) { return false; }

    constexpr uint32_t checkFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const bool hasMemoryType = std::ranges::any_of(memoryProperties.memoryTypes,[checkFlags](const auto& memoryType)
    {
        return (memoryType.propertyFlags & checkFlags) == checkFlags;
    });

    return hasMemoryType;
}