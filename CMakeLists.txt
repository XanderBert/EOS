cmake_minimum_required(VERSION 3.28)
project(EOS)

include(FetchContent)
include(cmake/FetchMacros.cmake)
include(cmake/CmakeMacros.cmake)

CREATE_APP(EOS)
set(EOS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(EOS_DEPENDENCIES_DIR ${EOS_ROOT_DIR}/dependencies)

#Setup Defines
if(WIN32)
    message(STATUS "Windows session detected")
    add_definitions("-DVK_USE_PLATFORM_WIN32_KHR")
    add_definitions("-DNOMINMAX")
    add_definitions("-DWIN32_LEAN_AND_MEAN")
    add_definitions("-DGLFW_EXPOSE_NATIVE_WIN32")
    add_definitions("-DEOS_PLATFORM_WINDOWS")
    set(USE_WINDOWS TRUE)
elseif (UNIX AND NOT APPLE)
    if (DEFINED ENV{WAYLAND_DISPLAY})
        message(STATUS "Wayland session detected")
        add_definitions("-DVK_USE_PLATFORM_WAYLAND_KHR")
        add_definitions("-DGLFW_EXPOSE_NATIVE_WAYLAND")
        add_definitions("-DEOS_PLATFORM_WAYLAND")
        set(USE_WAYLAND TRUE)
    else ()
        message(STATUS "X11 session detected")
        add_definitions("-DVK_USE_PLATFORM_XLIB_KHR")
        add_definitions("-DGLFW_EXPOSE_NATIVE_X11")
        add_definitions("-DEOS_PLATFORM_X11")
        set(USE_X11 TRUE)
    endif ()
endif ()


# Check if Vulkan SDK is installed and choose VOLK version
if(DEFINED ENV{VULKAN_SDK})
    # Extract the version number from the VULKAN_SDK environment variable
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" VULKAN_SDK_VERSION "$ENV{VULKAN_SDK}")
    message(STATUS "Detected Vulkan SDK version: ${VULKAN_SDK_VERSION}")

    # Set the VOLK_VERSION based on the detected Vulkan SDK version
    set(VOLK_VERSION "vulkan-sdk-${VULKAN_SDK_VERSION}.0")
    message(STATUS "Fetching VOLK version: ${VOLK_VERSION}")
else()
    # If VULKAN_SDK is not defined, try to find Vulkan using find_package
    find_package(Vulkan REQUIRED)

    if(Vulkan_FOUND)
        # Extract the version number from the Vulkan include directory
        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" VULKAN_SDK_VERSION "${Vulkan_VERSION}")
        message(STATUS "Detected Vulkan SDK version: ${VULKAN_SDK_VERSION}")

        # Set the VOLK_VERSION based on the detected Vulkan SDK version
        set(VOLK_VERSION "vulkan-sdk-${VULKAN_SDK_VERSION}.0")
        message(STATUS "Fetching VOLK version: ${VOLK_VERSION}")
    else()
        message(FATAL_ERROR "Vulkan SDK not found. Please install the Vulkan SDK.")
    endif()
endif()



FETCH_GLFW(3.4 ${EOS_DEPENDENCIES_DIR})
FETCH_VOLK(${VOLK_VERSION} ${EOS_DEPENDENCIES_DIR})
FETCH_VMA(v3.2.1 ${EOS_DEPENDENCIES_DIR})
FETCH_SPDLOG(${EOS_DEPENDENCIES_DIR})
#FETCH_SLANG(v2025.6 ${EOS_DEPENDENCIES_DIR})

target_link_libraries(EOS PUBLIC glfw)
target_link_libraries(EOS PRIVATE volk_headers)
target_link_libraries(EOS PRIVATE GPUOpen::VulkanMemoryAllocator)
target_link_libraries(EOS PRIVATE spdlog)
#target_link_libraries(EOS PRIVATE slang)
#target_link_libraries(EOS PRIVATE slang-rt)