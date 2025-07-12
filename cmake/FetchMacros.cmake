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

macro(FETCH_ASSIMP depsDir)
    set(ASSIMP_ROOT_DIR ${depsDir}/src/assimp)
    set(ASSIMP_BUILD_TESTS OFF)
    set(ASSIMP_INSTALL_PDB OFF)
    set(ASSIMP_BUILD_ASSIMP_VIEW OFF)
    set(ASSIMP_BUILD_IFC_IMPORTER OFF)
    set(ASSIMP_BUILD_X3D_IMPORTER OFF)
    set(ASSIMP_BUILD_AMF_IMPORTER OFF)
    set(ASSIMP_BUILD_3DS_IMPORTER OFF)
    set(ASSIMP_BUILD_AC_IMPORTER OFF)
    set(ASSIMP_BUILD_ASE_IMPORTER OFF)
    set(ASSIMP_BUILD_ASSBIN_IMPORTER OFF)
    set(ASSIMP_BUILD_B3D_IMPORTER OFF)
    set(ASSIMP_BUILD_D3D_IMPORTER OFF)
    set(ASSIMP_BUILD_BVH_IMPORTER OFF)
    set(ASSIMP_BUILD_COLLADA_IMPORTER OFF)
    set(ASSIMP_BUILD_DXF_IMPORTER OFF)
    set(ASSIMP_BUILD_CSM_IMPORTER OFF)
    set(ASSIMP_BUILD_HMP_IMPORTER OFF)
    set(ASSIMP_BUILD_IRRMESH_IMPORTER OFF)
    set(ASSIMP_BUILD_IQM_IMPORTER OFF)
    set(ASSIMP_BUILD_IRR_IMPORTER OFF)
    set(ASSIMP_BUILD_LWO_IMPORTER OFF)
    set(ASSIMP_BUILD_LWS_IMPORTER OFF)
    set(ASSIMP_BUILD_MD2_IMPORTER OFF)
    set(ASSIMP_BUILD_MD3_IMPORTER OFF)
    set(ASSIMP_BUILD_MD5_IMPORTER OFF)
    set(ASSIMP_BUILD_MDC_IMPORTER OFF)
    set(ASSIMP_BUILD_MDL_IMPORTER OFF)
    set(ASSIMP_BUILD_NFF_IMPORTER OFF)
    set(ASSIMP_BUILD_NDO_IMPORTER OFF)
    set(ASSIMP_BUILD_OFF_IMPORTER OFF)
    set(ASSIMP_BUILD_OGRE_IMPORTER OFF)
    set(ASSIMP_BUILD_OPENGEX_IMPORTER OFF)
    set(ASSIMP_BUILD_PLY_IMPORTER OFF)
    set(ASSIMP_BUILD_MS3D_IMPORTER OFF)
    set(ASSIMP_BUILD_COB_IMPORTER OFF)
    set(ASSIMP_BUILD_BLEND_IMPORTER OFF)
    set(ASSIMP_BUILD_XGL_IMPORTER OFF)
    set(ASSIMP_BUILD_Q3D_IMPORTER OFF)
    set(ASSIMP_BUILD_Q3BSP_IMPORTER OFF)
    set(ASSIMP_BUILD_RAW_IMPORTER OFF)
    set(ASSIMP_BUILD_SIB_IMPORTER OFF)
    set(ASSIMP_BUILD_SMD_IMPORTER OFF)
    set(ASSIMP_BUILD_STL_IMPORTER OFF)
    set(ASSIMP_BUILD_TERRAGEN_IMPORTER OFF)
    set(ASSIMP_BUILD_3D_IMPORTER OFF)
    set(ASSIMP_BUILD_X_IMPORTER OFF)
    set(ASSIMP_BUILD_X3D_IMPORTER OFF)
    set(ASSIMP_BUILD_3MF_IMPORTER OFF)
    set(ASSIMP_BUILD_MMD_IMPORTER OFF)
    set(ASSIMP_BUILD OFF)

    FetchContent_Populate(
            assimp
            GIT_REPOSITORY https://github.com/assimp/assimp
            GIT_TAG        v6.0.0
            SOURCE_DIR     ${ASSIMP_ROOT_DIR}
    )
    add_subdirectory(${ASSIMP_ROOT_DIR})
    target_link_libraries(EOS PRIVATE assimp::assimp)
endmacro()

macro(FETCH_GLM depsDir)

    set(GLM_ROOT_DIR ${depsDir}/src/glm)
    FetchContent_Populate(
            assimp
            GIT_REPOSITORY https://github.com/g-truc/glm
            GIT_TAG        1.0.1
            SOURCE_DIR     ${GLM_ROOT_DIR}
    )
    add_subdirectory(${GLM_ROOT_DIR})
    target_link_libraries(EOS PRIVATE glm::glm)

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
    FetchContent_Populate (
        fetch_ktx
        GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software
        GIT_TAG v4.4.0
        GIT_SHALLOW TRUE
        SOURCE_DIR     ${KTX_ROOT_DIR}
    )
    add_subdirectory(${KTX_ROOT_DIR})
    target_link_libraries(EOS PUBLIC ktx)
endmacro()