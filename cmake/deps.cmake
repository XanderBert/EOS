include_guard(GLOBAL)
include(FetchContent)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

function(eos_get_deps_paths OUT_SRC_DIR OUT_BIN_DIR)
    if(DEFINED EOS_DEPENDENCIES_DIR)
        set(deps_root "${EOS_DEPENDENCIES_DIR}")
    else()
        set(deps_root "${CMAKE_SOURCE_DIR}/dependencies")
    endif()

    set(${OUT_SRC_DIR} "${deps_root}/src" PARENT_SCOPE)
    set(${OUT_BIN_DIR} "${deps_root}/binaries" PARENT_SCOPE)
endfunction()

function(eos_set_option NAME VALUE)
    set(option_type STRING)
    if(ARGC GREATER 2)
        set(option_type "${ARGV2}")
    elseif("${VALUE}" STREQUAL "ON" OR "${VALUE}" STREQUAL "OFF")
        set(option_type BOOL)
    endif()

    set(${NAME} "${VALUE}" CACHE ${option_type} "" FORCE)
endfunction()

function(eos_dep NAME)
    set(options)
    set(oneValueArgs GIT TAG URL SOURCE_DIR)
    cmake_parse_arguments(DEP "${options}" "${oneValueArgs}" "" ${ARGN})

    eos_get_deps_paths(deps_src_dir deps_bin_dir)

    if(DEP_SOURCE_DIR)
        set(source_dir "${DEP_SOURCE_DIR}")
    else()
        set(source_dir "${deps_src_dir}/${NAME}")
    endif()

    if(DEP_GIT)
        FetchContent_Declare(
                ${NAME}
                GIT_REPOSITORY ${DEP_GIT}
                GIT_TAG ${DEP_TAG}
                GIT_SHALLOW TRUE
                UPDATE_DISCONNECTED TRUE
                SOURCE_DIR ${source_dir}
        )
    elseif(DEP_URL)
        FetchContent_Declare(
                ${NAME}
                URL ${DEP_URL}
                SOURCE_DIR ${source_dir}
        )
    else()
        message(FATAL_ERROR "eos_dep(${NAME}) requires either GIT or URL")
    endif()
endfunction()

function(eos_dep_populate NAME)
    FetchContent_GetProperties(${NAME})
    if(NOT ${NAME}_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${NAME})
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
        FetchContent_GetProperties(${NAME})
    endif()

    set(${NAME}_SOURCE_DIR "${${NAME}_SOURCE_DIR}" PARENT_SCOPE)
    set(${NAME}_BINARY_DIR "${${NAME}_BINARY_DIR}" PARENT_SCOPE)
    set(${NAME}_POPULATED "${${NAME}_POPULATED}" PARENT_SCOPE)
endfunction()

function(eos_dep_add_subdirectory NAME)
    set(options EXCLUDE_FROM_ALL)
    set(oneValueArgs BINARY_DIR)
    cmake_parse_arguments(DEP_ADD "${options}" "${oneValueArgs}" "" ${ARGN})

    eos_dep_populate(${NAME})
    set(dep_source_dir "${${NAME}_SOURCE_DIR}")

    if(NOT EXISTS "${dep_source_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Dependency '${NAME}' does not contain a CMakeLists.txt in ${dep_source_dir}")
    endif()

    if(DEP_ADD_BINARY_DIR)
        set(dep_binary_dir "${DEP_ADD_BINARY_DIR}")
    else()
        set(dep_binary_dir "${CMAKE_BINARY_DIR}/${NAME}-build")
    endif()

    if(DEP_ADD_EXCLUDE_FROM_ALL)
        add_subdirectory("${dep_source_dir}" "${dep_binary_dir}" EXCLUDE_FROM_ALL)
    else()
        add_subdirectory("${dep_source_dir}" "${dep_binary_dir}")
    endif()
endfunction()

function(eos_binary NAME)
    set(options)
    set(oneValueArgs URL ROOT_DIR ARCHIVE_NAME)
    cmake_parse_arguments(BIN "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT BIN_URL)
        message(FATAL_ERROR "eos_binary(${NAME}) requires URL")
    endif()

    eos_get_deps_paths(deps_src_dir deps_bin_dir)

    if(BIN_ROOT_DIR)
        set(root "${BIN_ROOT_DIR}")
    else()
        set(root "${deps_bin_dir}/${NAME}")
    endif()

    if(BIN_ARCHIVE_NAME)
        set(archive_path "${CMAKE_BINARY_DIR}/${BIN_ARCHIVE_NAME}")
    else()
        set(archive_path "${CMAKE_BINARY_DIR}/${NAME}.archive")
    endif()

    if(NOT EXISTS "${root}")
        message(STATUS "Downloading ${NAME}")
        file(DOWNLOAD "${BIN_URL}" "${archive_path}" SHOW_PROGRESS)
        file(MAKE_DIRECTORY "${root}")
        file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${root}")
    endif()

    set(${NAME}_ROOT "${root}" PARENT_SCOPE)
endfunction()

function(eos_require_glfw TARGET_NAME)
    eos_set_option(GLFW_BUILD_EXAMPLES OFF BOOL)
    eos_set_option(GLFW_BUILD_TESTS OFF BOOL)
    eos_set_option(GLFW_BUILD_DOCS OFF BOOL)
    eos_set_option(GLFW_INSTALL OFF BOOL)
    eos_set_option(GLFW_DOCUMENT_INTERNALS OFF BOOL)
    eos_set_option(GLFW_VULKAN_STATIC ON BOOL)
    eos_set_option(GLFW_USE_EGL OFF BOOL)
    eos_set_option(BUILD_SHARED_LIBS OFF BOOL)
    eos_set_option(GLFW_BUILD_WIN32 OFF BOOL)
    eos_set_option(GLFW_BUILD_WAYLAND OFF BOOL)
    eos_set_option(GLFW_BUILD_X11 OFF BOOL)

    target_compile_definitions(${TARGET_NAME} PRIVATE GLFW_INCLUDE_VULKAN)

    if(USE_WINDOWS)
        target_compile_definitions(${TARGET_NAME} PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
        eos_set_option(GLFW_BUILD_WIN32 ON BOOL)
    elseif(USE_WAYLAND)
        target_compile_definitions(${TARGET_NAME} PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
        eos_set_option(GLFW_BUILD_WAYLAND ON BOOL)
    elseif(USE_X11)
        target_compile_definitions(${TARGET_NAME} PRIVATE GLFW_EXPOSE_NATIVE_X11)
        eos_set_option(GLFW_BUILD_X11 ON BOOL)
    else()
        message(FATAL_ERROR "Could not detect OS for GLFW")
    endif()

    if(NOT TARGET glfw)
        eos_dep(glfw GIT https://github.com/glfw/glfw TAG ${EOS_DEP_GLFW_TAG})
        eos_dep_add_subdirectory(glfw BINARY_DIR "${CMAKE_BINARY_DIR}/glfw-build")
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC glfw)
endfunction()

function(eos_require_volk TARGET_NAME)
    set(options)
    set(oneValueArgs TAG)
    cmake_parse_arguments(VOLK "${options}" "${oneValueArgs}" "" ${ARGN})

    set(volk_tag "${EOS_DEP_VOLK_TAG}")
    if(VOLK_TAG)
        set(volk_tag "${VOLK_TAG}")
    endif()

    if(NOT TARGET volk_headers)
        eos_dep(volk GIT https://github.com/zeux/volk TAG "${volk_tag}")
        eos_dep_add_subdirectory(volk BINARY_DIR "${CMAKE_BINARY_DIR}/volk-build")
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE volk_headers)
endfunction()

function(eos_require_vulkan_utils TARGET_NAME)
    set(options)
    set(oneValueArgs TAG)
    cmake_parse_arguments(VUL "${options}" "${oneValueArgs}" "" ${ARGN})

    if(DEFINED EOS_VULKAN_UTILS_INCLUDE_DIR)
        target_include_directories(${TARGET_NAME} PRIVATE "${EOS_VULKAN_UTILS_INCLUDE_DIR}")
        return()
    endif()

    set(vulkan_utils_tag "${EOS_DEP_VULKAN_UTILS_TAG}")
    if(VUL_TAG)
        set(vulkan_utils_tag "${VUL_TAG}")
    endif()

    eos_dep(vulkan_utility_libraries GIT https://github.com/KhronosGroup/Vulkan-Utility-Libraries TAG "${vulkan_utils_tag}")
    eos_dep_populate(vulkan_utility_libraries)

    set(EOS_VULKAN_UTILS_INCLUDE_DIR "${vulkan_utility_libraries_SOURCE_DIR}/include" CACHE INTERNAL "" FORCE)
    target_include_directories(${TARGET_NAME} PRIVATE "${EOS_VULKAN_UTILS_INCLUDE_DIR}")
endfunction()

function(eos_require_vma TARGET_NAME)
    eos_set_option(VMA_BUILD_SAMPLES OFF BOOL)
    eos_set_option(VMA_ENABLE_INSTALL OFF BOOL)
    eos_set_option(VMA_BUILD_DOCUMENTATION OFF BOOL)

    if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
        eos_dep(vma GIT https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator TAG ${EOS_DEP_VMA_TAG})
        eos_dep_populate(vma)
        eos_dep_add_subdirectory(vma BINARY_DIR "${CMAKE_BINARY_DIR}/vma-build" SOURCE_DIR "${vma_SOURCE_DIR}")
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE GPUOpen::VulkanMemoryAllocator)
endfunction()

function(eos_require_spdlog TARGET_NAME)
    eos_set_option(SPDLOG_BUILD_EXAMPLES OFF BOOL)
    eos_set_option(SPDLOG_BUILD_BENCH OFF BOOL)
    eos_set_option(SPDLOG_BUILD_TESTS OFF BOOL)
    eos_set_option(SPDLOG_INSTALL OFF BOOL)
    eos_set_option(SPDLOG_BUILD_SHARED OFF BOOL)

    if(NOT TARGET spdlog)
        eos_dep(spdlog GIT https://github.com/gabime/spdlog.git TAG ${EOS_DEP_SPDLOG_TAG})
        eos_dep_add_subdirectory(spdlog BINARY_DIR "${CMAKE_BINARY_DIR}/spdlog-build")
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC spdlog)
endfunction()

function(eos_require_assimp TARGET_NAME)
    eos_set_option(ASSIMP_BUILD_TESTS OFF BOOL)
    eos_set_option(ASSIMP_INSTALL_PDB OFF BOOL)
    eos_set_option(ASSIMP_BUILD_ASSIMP_TOOLS OFF BOOL)
    eos_set_option(ASSIMP_BUILD_ASSIMP_VIEW OFF BOOL)
    eos_set_option(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF BOOL)
    eos_set_option(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF BOOL)
    eos_set_option(ASSIMP_BUILD_GLTF_IMPORTER ON BOOL)
    eos_set_option(ASSIMP_BUILD_SAMPLES OFF BOOL)
    eos_set_option(ASSIMP_NO_EXPORT ON BOOL)
    eos_set_option(ASSIMP_BUILD_ZLIB ON BOOL)

    if(NOT TARGET assimp::assimp)
        eos_dep(assimp GIT https://github.com/assimp/assimp TAG ${EOS_DEP_ASSIMP_TAG})
        eos_dep_add_subdirectory(assimp BINARY_DIR "${CMAKE_BINARY_DIR}/assimp-build")
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE assimp::assimp)
endfunction()

function(eos_require_glm TARGET_NAME)
    eos_set_option(GLM_BUILD_TESTS OFF BOOL)
    eos_set_option(GLM_BUILD_INSTALL OFF BOOL)

    if(NOT TARGET glm::glm)
        eos_dep(glm GIT https://github.com/g-truc/glm TAG ${EOS_DEP_GLM_TAG})
        eos_dep_add_subdirectory(glm BINARY_DIR "${CMAKE_BINARY_DIR}/glm-build")
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE glm::glm)
    target_compile_definitions(${TARGET_NAME} PRIVATE GLM_ENABLE_EXPERIMENTAL)

    if(EOS_VULKAN)
        target_compile_definitions(${TARGET_NAME} PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
    endif()
endfunction()

function(eos_require_tracy TARGET_NAME)
    if(NOT TARGET Tracy::TracyClient)
        eos_dep(tracy GIT https://github.com/wolfpld/tracy TAG ${EOS_DEP_TRACY_TAG})
        eos_dep_add_subdirectory(tracy BINARY_DIR "${CMAKE_BINARY_DIR}/tracy-build")
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC Tracy::TracyClient)
endfunction()

function(eos_require_stb TARGET_NAME)
    if(NOT TARGET EOS::stb)
        eos_dep(fetch_stb GIT https://github.com/nothings/stb TAG ${EOS_DEP_STB_TAG})
        eos_dep_populate(fetch_stb)

        add_library(EOS_stb INTERFACE)
        add_library(EOS::stb ALIAS EOS_stb)
        target_include_directories(EOS_stb INTERFACE "${fetch_stb_SOURCE_DIR}")
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC EOS::stb)
endfunction()

function(eos_require_ktx TARGET_NAME)
    eos_set_option(KTX_FEATURE_TESTS OFF BOOL)
    eos_set_option(KTX_FEATURE_TOOLS OFF BOOL)
    eos_set_option(KTX_FEATURE_DOC OFF BOOL)
    eos_set_option(KTX_FEATURE_STATIC_LIBRARY ON BOOL)
    eos_set_option(KTX_FEATURE_VULKAN ON BOOL)
    eos_set_option(KTX_FEATURE_GL_UPLOAD OFF BOOL)
    eos_set_option(KTX_FEATURE_VK_UPLOAD ON BOOL)

    if(NOT TARGET ktx)
        eos_dep(fetch_ktx GIT https://github.com/KhronosGroup/KTX-Software TAG ${EOS_DEP_KTX_TAG})
        eos_dep_add_subdirectory(fetch_ktx BINARY_DIR "${CMAKE_BINARY_DIR}/fetch_ktx-build")
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC ktx)
endfunction()

function(eos_require_imgui TARGET_NAME)
    if(NOT TARGET imgui)
        eos_dep(imgui GIT https://github.com/ocornut/imgui.git TAG ${EOS_DEP_IMGUI_TAG})
        eos_dep_populate(imgui)

        add_library(imgui STATIC
                ${imgui_SOURCE_DIR}/imgui.cpp
                ${imgui_SOURCE_DIR}/imgui_draw.cpp
                ${imgui_SOURCE_DIR}/imgui_tables.cpp
                ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        )
        target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
        target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
        target_link_libraries(imgui PUBLIC glfw)
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC imgui)
endfunction()

function(eos_locate_slang_layout ROOT_DIR OUT_INCLUDE OUT_LIB)
    set(slang_include "${ROOT_DIR}/include")
    set(slang_lib "${ROOT_DIR}/lib")

    if(EXISTS "${slang_include}" AND EXISTS "${slang_lib}")
        set(${OUT_INCLUDE} "${slang_include}" PARENT_SCOPE)
        set(${OUT_LIB} "${slang_lib}" PARENT_SCOPE)
        return()
    endif()

    file(GLOB child_dirs LIST_DIRECTORIES TRUE "${ROOT_DIR}/*")
    foreach(child_dir IN LISTS child_dirs)
        if(IS_DIRECTORY "${child_dir}" AND EXISTS "${child_dir}/include" AND EXISTS "${child_dir}/lib")
            set(${OUT_INCLUDE} "${child_dir}/include" PARENT_SCOPE)
            set(${OUT_LIB} "${child_dir}/lib" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "Failed to locate Slang include/lib directories in ${ROOT_DIR}")
endfunction()

function(eos_require_slang TARGET_NAME)
    if(WIN32)
        set(slang_url "${EOS_DEP_SLANG_WINDOWS_URL}")
        set(slang_archive_name "slang-windows.zip")
    elseif(UNIX AND NOT APPLE)
        set(slang_url "${EOS_DEP_SLANG_LINUX_URL}")
        set(slang_archive_name "slang-linux.tar.gz")
    else()
        message(FATAL_ERROR "Unsupported platform for Slang binaries")
    endif()

    eos_get_deps_paths(deps_src_dir deps_bin_dir)
    set(slang_root "${deps_bin_dir}/slang")

    eos_binary(slang URL "${slang_url}" ROOT_DIR "${slang_root}" ARCHIVE_NAME "${slang_archive_name}")
    eos_locate_slang_layout("${slang_ROOT}" slang_include slang_lib_dir)

    set(slang_include_dirs "${slang_include}")

    #Generate a slang include file
    if(NOT EXISTS "${slang_include}/slang-include.h")
        set(slang_compat_include "${CMAKE_BINARY_DIR}/generated/slang")
        file(MAKE_DIRECTORY "${slang_compat_include}")
        file(WRITE "${slang_compat_include}/slang-include.h" [=[
        #pragma once
        //This file is a include wrapper to resolve naming conflicts with X11
        //X11 defines None and Bool which are also used as enum values in Slang
        #if defined(EOS_PLATFORM_X11)
        #pragma push_macro("None")
        #undef None
        #pragma push_macro("Bool")
        #undef Bool
        #include <slang.h>
        #include <slang-com-ptr.h>
        #include <slang-com-helper.h>
        #pragma pop_macro("Bool")
        #pragma pop_macro("None")
        #else
        #include <slang.h>
        #include <slang-com-ptr.h>
        #include <slang-com-helper.h>
        #endif
        ]=])
        list(PREPEND slang_include_dirs "${slang_compat_include}")
    endif()

    if(NOT TARGET EOS::slang)
        add_library(EOS_slang INTERFACE)
        add_library(EOS::slang ALIAS EOS_slang)
        target_include_directories(EOS_slang INTERFACE ${slang_include_dirs})

        find_library(SLANG_LIBRARY
                NAMES slang
                PATHS "${slang_lib_dir}"
                NO_DEFAULT_PATH
        )

        if(NOT SLANG_LIBRARY)
            message(FATAL_ERROR "Could not find libslang in ${slang_lib_dir}")
        endif()

        target_link_libraries(EOS_slang INTERFACE "${SLANG_LIBRARY}")

        if(UNIX AND NOT APPLE)
            target_link_options(EOS_slang INTERFACE "-Wl,-rpath,${slang_lib_dir}")
        endif()

        find_program(EOS_SLANGC_EXECUTABLE
                NAMES slangc
                PATHS "${slang_ROOT}/bin"
                NO_DEFAULT_PATH
        )
        if(EOS_SLANGC_EXECUTABLE)
            set(EOS_SLANGC_EXECUTABLE "${EOS_SLANGC_EXECUTABLE}" CACHE FILEPATH "Path to slangc executable" FORCE)
        endif()
    endif()

    target_link_libraries(${TARGET_NAME} PUBLIC EOS::slang)

    if(WIN32)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${TARGET_NAME}>
                $<TARGET_FILE_DIR:${TARGET_NAME}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()