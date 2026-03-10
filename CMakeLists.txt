cmake_minimum_required(VERSION 3.28)
project(EOS)

set(EOS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(EOS_DEPENDENCIES_DIR ${EOS_ROOT_DIR}/dependencies)

include(cmake/CmakeMacros.cmake)
include(cmake/deps-lock.cmake)
include(cmake/deps.cmake)

CREATE_LIB(EOS)
DEFINE_PLATFORM()

option(EOS_VULKAN         "Enable Vulkan backend"        ON)
option(EOS_USE_IMGUI      "Enable ImGui integration"     ON)
option(EOS_USE_TRACY      "Enable Tracy profiler"        OFF)
option(EOS_BUILD_EXAMPLES "Build example applications"  ON)

# Set Debug or Release Define
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(EOS PRIVATE EOS_DEBUG)
    set(DEBUG TRUE)
else()
    target_compile_definitions(EOS PRIVATE EOS_RELEASE)
    set(DEBUG FALSE)
endif()
target_compile_definitions(EOS PRIVATE $<$<CONFIG:Debug>:EOS_DEBUG=1> $<$<NOT:$<CONFIG:Debug>>:EOS_RELEASE=1> )


# When we want to use Vulkan
if(EOS_VULKAN)
    target_compile_definitions(EOS PRIVATE EOS_VULKAN)
    target_compile_definitions(EOS PRIVATE VK_NO_PROTOTYPES) # Needed for VOLK

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
        # If it has a patch version, append it
        if(CMAKE_MATCH_5)
            set(VULKAN_SDK_VERSION "${VULKAN_SDK_VERSION}.${CMAKE_MATCH_5}")
        # If it doesn't have a patch version append .0
        else ()
            set(VULKAN_SDK_VERSION "${VULKAN_SDK_VERSION}.0")
        endif()


        set(SDK_VERSION "vulkan-sdk-${VULKAN_SDK_VERSION}")

    else()
        message(FATAL_ERROR "Vulkan SDK not found. Please install the Vulkan SDK.")
    endif()

    message(STATUS "Detected Vulkan SDK version: ${VULKAN_SDK_VERSION}")

    message(STATUS "Fetching VOLK version: ${SDK_VERSION}")
    eos_require_volk(EOS TAG ${SDK_VERSION})

    message(STATUS "Fetching Vulkan Utilities version: ${SDK_VERSION}")
    eos_require_vulkan_utils(EOS TAG ${SDK_VERSION})

    eos_require_vma(EOS)
else ()
    message(FATAL_ERROR "No Usable Graphics API Found")
endif ()

eos_require_glfw(EOS)
eos_require_spdlog(EOS)
eos_require_slang(EOS)
eos_require_stb(EOS)
eos_require_ktx(EOS)


if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g) # Add debug symbols
    add_compile_options(-O0) # The -O0 flag disables optimization
endif()


if(EOS_USE_TRACY)
    target_compile_definitions(EOS PRIVATE EOS_USE_TRACY)
    eos_require_tracy(EOS)
endif()

if(EOS_USE_IMGUI)
    target_compile_definitions(EOS PRIVATE EOS_USE_IMGUI)
    eos_require_imgui(EOS)
endif()

if(EOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()