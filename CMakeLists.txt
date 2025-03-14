cmake_minimum_required(VERSION 3.30)
project(EOS)

include(FetchContent)
include(cmake/FetchMacros.cmake)
include(cmake/CmakeMacros.cmake)

CREATE_APP(EOS)
set(EOS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(EOS_DEPENDENCIES_DIR ${EOS_ROOT_DIR}/dependencies)

#Check if Vulkan SDK is installed and choose VOLK version
if(DEFINED ENV{VULKAN_SDK})
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+" VULKAN_SDK_VERSION "$ENV{VULKAN_SDK}")
    message(STATUS "Detected Vulkan SDK version: ${VULKAN_SDK_VERSION}")
    set(VOLK_VERSION "vulkan-sdk-${VULKAN_SDK_VERSION}.0")
    message(STATUS "Fetching VOLK version: ${VOLK_VERSION}")
else()
    message(FATAL_ERROR "$ENV{VULKAN_SDK} does not exist.")
endif()

#Setup Defines
if(WIN32)
    add_definitions("-DVK_USE_PLATFORM_WIN32_KHR")
    add_definitions("-DNOMINMAX")
    add_definitions("-DWIN32_LEAN_AND_MEAN")
    add_definitions("-DGLFW_EXPOSE_NATIVE_WIN32")

    add_definitions("-DEOS_PLATFORM_WINDOWS")
endif()


FETCH_GLFW(3.4 ${EOS_DEPENDENCIES_DIR})
FETCH_VOLK(${VOLK_VERSION} ${EOS_DEPENDENCIES_DIR})
FETCH_SLANG(v2025.6 ${EOS_DEPENDENCIES_DIR})



target_link_libraries(EOS PUBLIC glfw)
target_link_libraries(EOS PRIVATE volk_headers)
#target_link_libraries(EOS PRIVATE slang)
#target_link_libraries(EOS PRIVATE slang-rt)