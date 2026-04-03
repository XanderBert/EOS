cmake_minimum_required(VERSION 3.28)
project(EOS)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(EOS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(EOS_DEPENDENCIES_DIR ${EOS_ROOT_DIR}/dependencies)

include(cmake/CmakeMacros.cmake)
include(cmake/deps-lock.cmake)
include(cmake/deps.cmake)

CREATE_LIB(EOS)

option(EOS_VULKAN "Enable Vulkan backend" ON)
option(EOS_USE_IMGUI "Enable ImGui integration" ON)
option(EOS_USE_TRACY "Enable Tracy profiler" ON)
option(EOS_BUILD_EXAMPLES "Build example applications" ON)
option(EOS_SHADER_TOOLS "Enable shader tools (hot reload, shader compiler, prebuild shader compile step)" ON)
option(EOS_BUILD_TEXTURE_TOOLS "Build the texture compressor tool" ON)

# User-configurable shader paths for runtime and shader compilation.
set(EOS_PROJECT_SHADER_PATH "" CACHE PATH "Path to project shaders used by examples. Leave empty to use each example's src/shaders directory")
set(EOS_SHADER_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Path where compiled shader binaries are written")

# Skip prebuild in GitHub Actions
if(DEFINED ENV{GITHUB_ACTIONS} AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(EOS_SHADER_TOOLS OFF CACHE BOOL "Enable shader tools (hot reload, shader compiler, prebuild shader compile step)" FORCE)
    message(STATUS "GitHub Actions detected") 
endif()

EOS_PRINT_CONFIGURATION_SUMMARY()


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
    target_compile_definitions(EOS PUBLIC EOS_VULKAN)
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
eos_require_ktx(EOS)

if(EOS_SHADER_TOOLS)
    if(NOT DEFINED EOS_SHADER_OUTPUT_PATH OR EOS_SHADER_OUTPUT_PATH STREQUAL "")
        message(FATAL_ERROR "EOS_SHADER_OUTPUT_PATH must be set when EOS_SHADER_TOOLS is ON")
        message(STATUS "Please call SET_SHADER_PATHS(${PROJECT_NAME} <project_shader_path> <shader_output_path>) in your CMakeLists.txt to set the paths for shader compilation")
    endif()

    if(DEFINED EOS_PROJECT_SHADER_PATH AND NOT EOS_PROJECT_SHADER_PATH STREQUAL "" AND NOT IS_DIRECTORY "${EOS_PROJECT_SHADER_PATH}")
        message(FATAL_ERROR "EOS_PROJECT_SHADER_PATH is set but does not exist: ${EOS_PROJECT_SHADER_PATH}")
        message(STATUS "Please call SET_SHADER_PATHS(${PROJECT_NAME} <project_shader_path> <shader_output_path>) in your CMakeLists.txt to set the paths for shader compilation")
    endif()

    set(ENGINE_SHADER_PATH "${CMAKE_SOURCE_DIR}/src/shaders")
    target_compile_definitions(EOS PRIVATE EOS_SHADER_TOOLS)
    target_include_directories(EOS PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    target_sources(EOS PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/ShaderTools/shaderCompiler.cpp"
        "${CMAKE_SOURCE_DIR}/tools/ShaderTools/shaderReloader.cpp"
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE EOS_ENGINE_SHADER_PATH="${ENGINE_SHADER_PATH}")
    eos_require_slang(EOS)
endif ()

if(EOS_USE_TRACY)
    target_compile_definitions(EOS PUBLIC EOS_USE_TRACY)
    eos_require_tracy(EOS)
endif()

if(EOS_USE_IMGUI)
    target_compile_definitions(EOS PRIVATE EOS_USE_IMGUI)
    eos_require_imgui(EOS)
endif()

if(EOS_BUILD_TEXTURE_TOOLS OR EOS_SHADER_TOOLS)
    add_subdirectory(tools)
endif()

if(EOS_BUILD_TEXTURE_TOOLS)
    eos_require_stb(EOS)
    target_compile_definitions(EOS PRIVATE EOS_BUILD_TEXTURE_TOOLS)
    target_include_directories(EOS PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    target_sources(EOS PRIVATE "${CMAKE_SOURCE_DIR}/tools/TextureTools/texture_packer.cpp")
endif()

if(EOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()