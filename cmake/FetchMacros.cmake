macro(FETCH_GLFW tag depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    # Fetch GLFW and Setup
    option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
    option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
    option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
    option(GLFW_INSTALL "Generate installation target" OFF)
    option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)

    if(UNIX AND NOT APPLE)
        # Disable win32
        set(GLFW_BUILD_WIN32 OFF)

        find_package(Wayland QUIET)
        if(WAYLAND_FOUND)
            set(GLFW_BUILD_WAYLAND ON)
            set(GLFW_BUILD_X11 OFF)
        else()
            set(GLFW_BUILD_WAYLAND OFF)
            set(GLFW_BUILD_X11 ON)
        endif()
    else()
        set(GLFW_BUILD_WAYLAND OFF)
        set(GLFW_BUILD_X11 OFF)
    endif()

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
            GIT_REPOSITORY https://github.com/shader-slang/slang
            GIT_TAG        ${TAG}
            SOURCE_DIR     ${SLANG_ROOT_DIR}
    )
    add_subdirectory(${SLANG_ROOT_DIR})
endmacro()