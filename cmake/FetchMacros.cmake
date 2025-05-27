macro(FETCH_GLFW depsDir)
    set(DEPS_DIR ${depsDir})

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

    target_compile_definitions(EOS PRIVATE GLFW_INCLUDE_VULKAN)

    if(USE_WINDOWS)
        target_compile_definitions(EOS PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
        set(GLFW_BUILD_WIN32 ON)
    elseif (USE_WAYLAND)
        target_compile_definitions(EOS PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
        set(GLFW_BUILD_WAYLAND ON)
    elseif (USE_X11)
        target_compile_definitions(EOS PRIVATE GLFW_EXPOSE_NATIVE_X11)
        set(GLFW_BUILD_X11 ON)
    else ()
        message(FATAL_ERROR "Could not detect OS for GLFW")
    endif ()


    set(GLFW_ROOT_DIR ${DEPS_DIR}/src/glfw)
    FetchContent_Populate(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw
            GIT_TAG        3.4
            SOURCE_DIR     ${GLFW_ROOT_DIR}
    )
    add_subdirectory(${GLFW_ROOT_DIR})
    target_link_libraries(EOS PUBLIC glfw)
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
    target_link_libraries(EOS PRIVATE volk_headers)
endmacro()

macro(FETCH_VMA depsDir)
    set(DEPS_DIR ${depsDir})

    set(VMA_ROOT_DIR ${DEPS_DIR}/src/vma)
    FetchContent_Populate(
            vma
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
            GIT_TAG        v3.2.1
            SOURCE_DIR     ${VMA_ROOT_DIR}
    )

    add_subdirectory(${VMA_ROOT_DIR})
    target_link_libraries(EOS PRIVATE GPUOpen::VulkanMemoryAllocator)
endmacro()

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
    target_link_libraries(EOS PRIVATE spdlog)
endmacro()

macro(FETCH_SLANG)
    # Set the base path for Slang
    set(SLANG_BASE_PATH "${CMAKE_SOURCE_DIR}/dependencies/binaries/slang")

    # Find the library
    find_library(SLANG_LIBRARY
            NAMES slang libslang slang.lib
            PATHS "${SLANG_BASE_PATH}/lib"
            NO_DEFAULT_PATH
    )

    # Check if library was found
    if(NOT SLANG_LIBRARY)
        message(WARNING "Slang library not found at ${SLANG_BASE_PATH}/lib")
        # You could add a fallback here, like downloading it automatically
    else()

        message(STATUS "Found Slang library: ${SLANG_LIBRARY}")
        target_link_libraries(EOS PRIVATE ${SLANG_LIBRARY})

        # Add include directory if it exists
        if(EXISTS "${SLANG_BASE_PATH}/include")
            include_directories("${SLANG_BASE_PATH}/include")
            message(STATUS "Added Slang include directory: ${SLANG_BASE_PATH}/include")
        else()
            message(WARNING "Slang include directory not found at ${SLANG_BASE_PATH}/include")
        endif()
    endif()
endmacro()