macro(FETCH_GLFW tag depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    # Fetch GLFW and Setup
    set(GLFW_BUILD_EXAMPLES          OFF CACHE BOOL "")
    set(GLFW_BUILD_TESTS             OFF CACHE BOOL "")
    set(GLFW_BUILD_DOCS              OFF CACHE BOOL "")
    set(GLFW_INSTALL                 OFF CACHE BOOL "")
    set(GLFW_DOCUMENT_INTERNALS      OFF CACHE BOOL "")
    set(GLFW_VULKAN_STATIC           ON CACHE BOOL "")
    set(GLFW_USE_EGL                 OFF CACHE BOOL "")


    set(GLFW_BUILD_WIN32 OFF)
    set(GLFW_BUILD_WAYLAND OFF)
    set(GLFW_BUILD_X11 OFF)

    if(USE_WINDOWS)
        set(GLFW_BUILD_WIN32 ON)
    elseif (USE_WAYLAND)
        set(GLFW_BUILD_WAYLAND ON)
    elseif (USE_X11)
        set(GLFW_BUILD_X11 ON)
    else ()
        message(FATAL_ERROR "Could not detect OS for GLFW")
    endif ()


    set(GLFW_ROOT_DIR ${DEPS_DIR}/src/glfw)
    FetchContent_Populate(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw
            GIT_TAG        ${TAG}
            SOURCE_DIR     ${GLFW_ROOT_DIR}
    )
    add_subdirectory(${GLFW_ROOT_DIR})
endmacro()

macro(FETCH_VOLK tag, depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    set(VOLK_ROOT_DIR ${DEPS_DIR}/src/volk)
    FetchContent_Populate(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk
            GIT_TAG        ${TAG}
            SOURCE_DIR     ${VOLK_ROOT_DIR}
    )

    add_subdirectory(${VOLK_ROOT_DIR})
endmacro()

macro(FETCH_VMA tag, depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    set(VMA_ROOT_DIR ${DEPS_DIR}/src/vma)
    FetchContent_Populate(
            vma
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
            GIT_TAG        ${TAG}
            SOURCE_DIR     ${VMA_ROOT_DIR}
    )

    add_subdirectory(${VMA_ROOT_DIR})
endmacro()

macro(FETCH_SLANG tag, depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    #Disable Slang tests and examples
    set(SLANG_ENABLE_CUDA          OFF CACHE BOOL "")
    set(SLANG_ENABLE_OPTIX         OFF CACHE BOOL "")
    set(SLANG_ENABLE_NVAPI         OFF CACHE BOOL "")
    set(SLANG_ENABLE_XLIB          OFF CACHE BOOL "")
    set(SLANG_ENABLE_AFTERMATH     OFF CACHE BOOL "")
    set(SLANG_ENABLE_DX_ON_VK      OFF CACHE BOOL "")
    set(SLANG_ENABLE_GFX           OFF CACHE BOOL "")
    set(SLANG_ENABLE_SLANGC        OFF CACHE BOOL "")
    set(SLANG_ENABLE_SLANGRT       ON  CACHE BOOL "")
    set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "")
    set(SLANG_ENABLE_TESTS         OFF CACHE BOOL "")
    set(SLANG_ENABLE_EXAMPLES      OFF CACHE BOOL "")
    set(SLANG_ENABLE_REPLAYER      OFF CACHE BOOL "")
    set(SLANG_ENABLE_PREBUILT_BINARIES OFF CACHE BOOL "")

    set(SLANG_ROOT_DIR ${DEPS_DIR}/src/slang)
    FetchContent_Populate(
            slang
            GIT_REPOSITORY https://github.com/shader-slang/slang.git
            GIT_TAG        ${TAG}
            SOURCE_DIR     ${SLANG_ROOT_DIR}
    )
    add_subdirectory(${SLANG_ROOT_DIR})
endmacro()



#TODO: When passing tag here, i get the first ever pre release of this library.
#Why is that? when i manually set the git tag it gets the correct version
macro(FETCH_SPDLOG depsDir)
    set(DEPS_DIR ${depsDir})

    set(SPDLOG_ROOT_DIR ${DEPS_DIR}/src/spdlog)
    FetchContent_Populate(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG        v1.15.2
            SOURCE_DIR     ${SPDLOG_ROOT_DIR}
    )
    add_subdirectory(${SPDLOG_ROOT_DIR})
endmacro()