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



FETCH_GLFW(3.4 ${EOS_DEPENDENCIES_DIR})
FETCH_VOLK(${VOLK_VERSION} ${EOS_DEPENDENCIES_DIR})
FETCH_SLANG(v2025.6 ${EOS_DEPENDENCIES_DIR})



target_include_directories(EOS PUBLIC "${EOS_DEPENDENCIES_DIR}/src/volk")
target_link_libraries(EOS PUBLIC glfw)
#target_link_libraries(EOS PRIVATE slang)
#target_link_libraries(EOS PRIVATE slang-rt)