macro(FETCH_GLFW depsDir)
    set(DEPS_DIR ${depsDir})

    set(GLFW_BUILD_EXAMPLES          OFF CACHE BOOL "")
    set(GLFW_BUILD_TESTS             OFF CACHE BOOL "")
    set(GLFW_BUILD_DOCS              OFF CACHE BOOL "")
    set(GLFW_INSTALL                 OFF CACHE BOOL "")
    set(GLFW_DOCUMENT_INTERNALS      OFF CACHE BOOL "")
    set(GLFW_VULKAN_STATIC           ON CACHE BOOL "")
    set(GLFW_USE_EGL                 OFF CACHE BOOL "")
    set(BUILD_SHARED_LIBS            OFF CACHE BOOL "")
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

macro(FETCH_VOLK tag depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})

    set(VOLK_ROOT_DIR ${DEPS_DIR}/src/volk)
    FetchContent_Populate(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk
            GIT_TAG        ${TAG}
            GIT_SHALLOW    TRUE
            SOURCE_DIR     ${VOLK_ROOT_DIR}
    )

    add_subdirectory(${VOLK_ROOT_DIR})
    target_link_libraries(EOS PRIVATE volk_headers)
endmacro()

macro(FETCH_VULKAN_UTILS tag depsDir)
    set(DEPS_DIR ${depsDir})
    set(TAG ${tag})
    set(VULKAN_TOOLS_ROOT_DIR ${DEPS_DIR}/src/vulkan-utility-libraries)

    FetchContent_Populate(
            vulkan_utility_libraries
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries
            GIT_TAG        ${TAG}
            GIT_SHALLOW    TRUE
            SOURCE_DIR     ${VULKAN_TOOLS_ROOT_DIR}
    )
    include_directories(${DEPS_DIR}/src/vulkan-utility-libraries/include)
endmacro()

macro(FETCH_VMA depsDir)
    set(DEPS_DIR ${depsDir})

    set(VMA_BUILD_SAMPLES        OFF CACHE BOOL "")
    set(VMA_ENABLE_INSTALL       OFF CACHE BOOL "")
    set(VMA_BUILD_DOCUMENTATION  OFF CACHE BOOL "")

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

    set(SPDLOG_BUILD_EXAMPLES   OFF CACHE BOOL "")
    set(SPDLOG_BUILD_BENCH      OFF CACHE BOOL "")
    set(SPDLOG_BUILD_TESTS      OFF CACHE BOOL "")
    set(SPDLOG_INSTALL          OFF CACHE BOOL "")
    set(SPDLOG_BUILD_SHARED     OFF CACHE BOOL "")

    set(SPDLOG_ROOT_DIR ${DEPS_DIR}/src/spdlog)
    FetchContent_Populate(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG        v1.15.2
            SOURCE_DIR     ${SPDLOG_ROOT_DIR}
    )
    add_subdirectory(${SPDLOG_ROOT_DIR})
    target_link_libraries(EOS PUBLIC spdlog)
endmacro()

macro(FETCH_SLANG)
    set(SLANG_BASE_PATH "${CMAKE_SOURCE_DIR}/dependencies/binaries/slang")

    find_library(SLANG_LIBRARY
            NAMES slang libslang slang.lib
            PATHS "${SLANG_BASE_PATH}/lib"
            NO_DEFAULT_PATH
    )

    if(NOT SLANG_LIBRARY)
        message(WARNING "Slang library not found at ${SLANG_BASE_PATH}/lib")
    else()
        message(STATUS "Found Slang library: ${SLANG_LIBRARY}")
        target_link_libraries(EOS PRIVATE ${SLANG_LIBRARY})

        if(EXISTS "${SLANG_BASE_PATH}/include")
            include_directories("${SLANG_BASE_PATH}/include")
            message(STATUS "Added Slang include directory: ${SLANG_BASE_PATH}/include")
        else()
            message(WARNING "Slang include directory not found at ${SLANG_BASE_PATH}/include")
        endif()
    endif()
endmacro()

macro(FETCH_ASSIMP depsDir targetName)
    if(NOT TARGET assimp::assimp)
        set(ASSIMP_ROOT_DIR ${depsDir}/src/assimp)

         set(ASSIMP_BUILD_TESTS OFF)
         set(ASSIMP_INSTALL_PDB OFF)
         set(ASSIMP_BUILD_ASSIMP_TOOLS OFF)
         set(ASSIMP_BUILD_ASSIMP_VIEW OFF)
         set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)
         set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
         set(ASSIMP_BUILD_GLTF_IMPORTER ON)
         set(ASSIMP_BUILD_SAMPLES OFF)
         set(ASSIMP_NO_EXPORT ON)
         set(ASSIMP_BUILD_ZLIB ON)

         FetchContent_Populate(
                 assimp
                 GIT_REPOSITORY https://github.com/assimp/assimp
                 GIT_TAG v6.0.4
                 GIT_SHALLOW TRUE
                 SOURCE_DIR ${ASSIMP_ROOT_DIR}
         )
         add_subdirectory(${ASSIMP_ROOT_DIR} ${CMAKE_BINARY_DIR}/dependencies/src/assimp)
    endif ()

         target_link_libraries(${targetName} PRIVATE assimp::assimp)

endmacro()

macro(FETCH_GLM depsDir targetName)

    set(GLM_BUILD_TESTS     OFF CACHE BOOL "")
    set(GLM_BUILD_INSTALL   OFF CACHE BOOL "")

    if(NOT TARGET glm::glm)
        set(GLM_ROOT_DIR ${depsDir}/src/glm)
        FetchContent_Populate(
                glm
                GIT_REPOSITORY https://github.com/g-truc/glm
                GIT_TAG        1.0.1
                SOURCE_DIR     ${GLM_ROOT_DIR}
        )
        add_subdirectory(${GLM_ROOT_DIR} ${CMAKE_BINARY_DIR}/dependencies/src/glm)
    endif()

    target_link_libraries(${targetName} PRIVATE glm::glm)
    target_compile_definitions(${targetName} PRIVATE GLM_ENABLE_EXPERIMENTAL)

    if (EOS_VULKAN)
        target_compile_definitions(${targetName} PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
    endif ()

endmacro()

macro(FETCH_TRACY depsDir)
    set(TRACY_ROOT_DIR ${depsDir}/src/tracy)
    FetchContent_Populate (
            tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy
            GIT_TAG master
            GIT_SHALLOW TRUE
            SOURCE_DIR     ${TRACY_ROOT_DIR}
    )
    add_subdirectory(${TRACY_ROOT_DIR})
    target_link_libraries(EOS PUBLIC Tracy::TracyClient)
endmacro()

macro(FETCH_STB depsDir)
    set(STBI_ROOT_DIR ${depsDir}/src/stb)
    FetchContent_Populate (
            fetch_stb
            GIT_REPOSITORY https://github.com/nothings/stb
            GIT_SHALLOW TRUE
            SOURCE_DIR     ${STBI_ROOT_DIR}
    )
    include_directories(${STBI_ROOT_DIR})
endmacro()

macro(FETCH_KTX depsDir)
    set(KTX_ROOT_DIR ${depsDir}/src/ktx)

    set(KTX_FEATURE_TESTS           OFF CACHE BOOL "")
    set(KTX_FEATURE_TOOLS           OFF CACHE BOOL "")
    set(KTX_FEATURE_DOC             OFF CACHE BOOL "")
    set(KTX_FEATURE_STATIC_LIBRARY  ON  CACHE BOOL "")
    set(KTX_FEATURE_VULKAN          ON  CACHE BOOL "")
    set(KTX_FEATURE_GL_UPLOAD       OFF CACHE BOOL "")
    set(KTX_FEATURE_VK_UPLOAD       ON  CACHE BOOL "")

    FetchContent_Populate (
            fetch_ktx
            GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software
            GIT_TAG v4.4.2
            GIT_SHALLOW TRUE
            SOURCE_DIR     ${KTX_ROOT_DIR}
    )
    add_subdirectory(${KTX_ROOT_DIR})
    target_link_libraries(EOS PUBLIC ktx)
endmacro()


macro(FETCH_IMGUI depsDir)
    set(DEPS_DIR ${depsDir})

    set(IMGUI_ROOT_DIR ${DEPS_DIR}/src/imgui)
    FetchContent_Populate(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.92.6
            GIT_SHALLOW TRUE
            SOURCE_DIR     ${IMGUI_ROOT_DIR}
    )

    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
    target_link_libraries(imgui PUBLIC glfw)
    target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(EOS PUBLIC imgui)
endmacro()