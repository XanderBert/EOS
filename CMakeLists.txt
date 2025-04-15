cmake_minimum_required(VERSION 3.28)
project(EOS)

include(FetchContent)
include(cmake/FetchMacros.cmake)
include(cmake/CmakeMacros.cmake)

CREATE_APP(EOS)
set(EOS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(EOS_DEPENDENCIES_DIR ${EOS_ROOT_DIR}/dependencies)

#For now we only use the Vulkan API
set(EOS_VULKAN TRUE)

#Setup Platform Defines
if(WIN32)
    message(STATUS "Windows session detected")
    target_compile_definitions(EOS PRIVATE NOMINMAX)
    target_compile_definitions(EOS PRIVATE WIN32_LEAN_AND_MEAN)
    target_compile_definitions(EOS PRIVATE EOS_PLATFORM_WINDOWS)
    set(USE_WINDOWS TRUE)
elseif (UNIX AND NOT APPLE)
    if (DEFINED ENV{WAYLAND_DISPLAY})
        message(STATUS "Wayland session detected")
        target_compile_definitions(EOS PRIVATE EOS_PLATFORM_WAYLAND)
        set(USE_WAYLAND TRUE)
    else ()
        message(STATUS "X11 session detected")
        target_compile_definitions(EOS PRIVATE EOS_PLATFORM_X11)
        set(USE_X11 TRUE)
    endif ()
endif ()

# Set Debug or Release Define
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(EOS PRIVATE EOS_DEBUG)
else()
    target_compile_definitions(EOS PRIVATE EOS_RELEASE)
endif()
target_compile_definitions(EOS PRIVATE $<$<CONFIG:Debug>:EOS_DEBUG=1> $<$<NOT:$<CONFIG:Debug>>:EOS_RELEASE=1> )


# When we want to use Vulkan
if(EOS_VULKAN)
    target_compile_definitions(EOS PRIVATE EOS_VULKAN)
    target_compile_definitions(EOS PRIVATE VK_NO_PROTOTYPES)

    if(USE_WINDOWS)
        target_compile_definitions(EOS PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    elseif (USE_WAYLAND)
        target_compile_definitions(EOS PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    elseif (USE_X11)
        target_compile_definitions(EOS PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    endif ()

    # Check if Vulkan SDK is installed and choose VOLK version
    find_package(Vulkan REQUIRED)
    if(Vulkan_FOUND)
        # Extract full version including possible patch version
        string(REGEX MATCH "([0-9]+)\.([0-9]+)\.([0-9]+)(\.([0-9]+))?" _ "${Vulkan_VERSION}")
        set(VULKAN_SDK_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
        if(CMAKE_MATCH_5)
            set(VULKAN_SDK_VERSION "${VULKAN_SDK_VERSION}.${CMAKE_MATCH_5}")
        endif()

        message(STATUS "Detected Vulkan SDK version: ${VULKAN_SDK_VERSION}")
        set(VOLK_VERSION "vulkan-sdk-${VULKAN_SDK_VERSION}")
        message(STATUS "Fetching VOLK version: ${VOLK_VERSION}")
    else()
        message(FATAL_ERROR "Vulkan SDK not found. Please install the Vulkan SDK.")
    endif()

    FETCH_VOLK(${VOLK_VERSION} ${EOS_DEPENDENCIES_DIR})
    FETCH_VMA(v3.2.1 ${EOS_DEPENDENCIES_DIR})

    target_link_libraries(EOS PRIVATE volk_headers)
    target_link_libraries(EOS PRIVATE GPUOpen::VulkanMemoryAllocator)
elseif ()
    message(FATAL_ERROR "No Usable Graphics API Found")
endif ()


FETCH_GLFW(3.4 ${EOS_DEPENDENCIES_DIR})
FETCH_SPDLOG(${EOS_DEPENDENCIES_DIR})

target_link_libraries(EOS PUBLIC glfw)
target_link_libraries(EOS PRIVATE spdlog)