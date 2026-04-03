macro(SETUP_GROUPS src_files)
    foreach(FILE ${src_files})
        get_filename_component(PARENT_DIR "${FILE}" PATH)

        set(GROUP "${PARENT_DIR}")
        string(REPLACE "." "\\" GROUP "${GROUP}")

        source_group("${GROUP}" FILES "${FILE}")
    endforeach()
endmacro()

macro(SETUP_TARGET_DEFAULTS targetName)
    set_property(TARGET ${targetName} PROPERTY CXX_STANDARD 20)
    set_property(TARGET ${targetName} PROPERTY CXX_STANDARD_REQUIRED ON)

    if(MSVC)
        set_property(TARGET ${targetName} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
    endif()
endmacro()

macro(SETUP_X11_NONE_WORKAROUND targetName)
    set(EOS_X11_UNDEFINE_NONE_HEADER "${CMAKE_BINARY_DIR}/generated/EOSX11UndefineNone.h")

    file(GENERATE OUTPUT "${EOS_X11_UNDEFINE_NONE_HEADER}" CONTENT [=[
        #pragma once
        #if defined(EOS_PLATFORM_X11)
        #include <X11/X.h>
        
        // https://searchfox.org/mozilla-central/source/gfx/src/X11UndefineNone.h
        // The header <X11/X.h> defines "None" as a macro that expands to "0L".
        // This is terrible because many enumerations have an enumerator named "None".
        // To work around this, we undefine the macro "None", and define a replacement
        // macro named "X11None".
        #ifdef None
        #  undef None
        #  define X11None 0L
        // <X11/X.h> also defines "RevertToNone" as a macro that expands to "(int)None".
        // Since we are undefining "None", that stops working. To keep it working,
        // we undefine "RevertToNone" and redefine it in terms of "X11None".
        #  ifdef RevertToNone
        #    undef RevertToNone
        #    define RevertToNone (int)X11None
        #  endif
        #endif
        #endif
]=])

    target_compile_options(${targetName} PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:-include${EOS_X11_UNDEFINE_NONE_HEADER}>")
endmacro()

function(EOS_PRINT_CONFIGURATION_SUMMARY)
    message(STATUS "")
    message(STATUS "================ EOS Configuration Summary ================")

    set(EOS_CONFIG_SUMMARY_VARS
        EOS_VULKAN
        EOS_USE_IMGUI
        EOS_USE_TRACY
        EOS_BUILD_EXAMPLES
        EOS_SHADER_TOOLS
        EOS_BUILD_TEXTURE_TOOLS
        EOS_SHADER_OUTPUT_PATH
    )

    foreach(EOS_SUMMARY_VAR IN LISTS EOS_CONFIG_SUMMARY_VARS)
        if(DEFINED ${EOS_SUMMARY_VAR})
            message(STATUS "  ${EOS_SUMMARY_VAR} = [${${EOS_SUMMARY_VAR}}]")
        else()
            message(STATUS "  ${EOS_SUMMARY_VAR} = <UNDEFINED>")
        endif()
    endforeach()

    message(STATUS "==========================================================")
    message(STATUS "")
endfunction()

macro(CREATE_LIB name)
    set(PROJECT_NAME ${name})
    project(${PROJECT_NAME} CXX)

    if(NOT DEFINED LIB_TYPE)
        set(LIB_TYPE STATIC)
    endif()

    file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false src/*.c??)
    file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false src/*.h)

    message(STATUS "SRC_FILES: ${SRC_FILES}")
    message(STATUS "HEADER_FILES: ${HEADER_FILES}")

    add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES} ${HEADER_FILES})

    target_include_directories(${PROJECT_NAME}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
    )

    SETUP_GROUPS("${SRC_FILES}")
    SETUP_GROUPS("${HEADER_FILES}")

    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_DEBUG ${PROJECT_NAME}_Debug)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_RELEASE ${PROJECT_NAME}_Release)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_RELWITHDEBINFO ${PROJECT_NAME}_ReleaseDebInfo)

    if(UNIX)
        set_target_properties(${PROJECT_NAME} PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib"
        )
    endif()

    SETUP_TARGET_DEFAULTS(${PROJECT_NAME})

    if(MSVC)
        set_property(TARGET ${PROJECT_NAME} PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()

    DEFINE_PLATFORM()
endmacro()


function(ADD_SHADER_COMPILATION_TARGET TARGET_NAME PROJECT_SHADER_PATH ENGINE_SHADER_PATH OUTPUT_PATH SHADER_FILES_LIST)
    if(NOT TARGET EOSShaderCompilerTool)
        return()
    endif()

    set(SHADER_COMPILE_TARGET "CompileShaders_${TARGET_NAME}")
    
    add_custom_target(${SHADER_COMPILE_TARGET}
        COMMAND $<TARGET_FILE:EOSShaderCompilerTool>
            --project-shaders "${PROJECT_SHADER_PATH}"
            --engine-shaders "${ENGINE_SHADER_PATH}"
            --output "${OUTPUT_PATH}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Compiling shaders for ${TARGET_NAME}"
        USES_TERMINAL
        VERBATIM
        DEPENDS ${SHADER_FILES_LIST}
    )
    set_property(TARGET ${SHADER_COMPILE_TARGET} PROPERTY FOLDER "Internal/Shaders")
    add_dependencies(${SHADER_COMPILE_TARGET} EOSShaderCompilerTool)
    add_dependencies(${TARGET_NAME} ${SHADER_COMPILE_TARGET})
endfunction()

macro(SET_SHADER_PATHS TARGET_NAME PROJECT_SHADER_PATH_VALUE SHADER_OUTPUT_PATH_VALUE)
    target_compile_definitions(${TARGET_NAME} PRIVATE EOS_PROJECT_SHADER_PATH="${PROJECT_SHADER_PATH_VALUE}")
    target_compile_definitions(${TARGET_NAME} PRIVATE EOS_SHADER_OUTPUT_PATH="${SHADER_OUTPUT_PATH_VALUE}")
    message(STATUS "[${TARGET_NAME}] PROJECT_SHADER_PATH = [${PROJECT_SHADER_PATH_VALUE}]")
    message(STATUS "[${TARGET_NAME}] SHADER_OUTPUT_PATH = [${SHADER_OUTPUT_PATH_VALUE}]")
endmacro()

macro(CREATE_EXAMPLE name)
    set(PROJECT_NAME ${name})
    project(${PROJECT_NAME} CXX)

    file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false src/*.c??)

    add_executable(${PROJECT_NAME} ${SRC_FILES})
    set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Examples")

    target_link_libraries(${PROJECT_NAME} PRIVATE EOS)

    set(PROJECT_SHADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
    set(SHADER_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin")
    SET_SHADER_PATHS(${PROJECT_NAME} "${PROJECT_SHADER_PATH}" "${SHADER_OUTPUT_PATH}")
    SETUP_GROUPS("${SRC_FILES}")

    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
    SETUP_TARGET_DEFAULTS(${PROJECT_NAME})


    ADD_SHADER_COMPILATION_TARGET(
        ${PROJECT_NAME}
        "${PROJECT_SHADER_PATH}"
        "${ENGINE_SHADER_PATH}"
        "${SHADER_OUTPUT_PATH}"
        "${SHADER_FILES}"
    )
endmacro()

macro(DEFINE_PLATFORM)
    if(WIN32)
        message(STATUS "Windows session detected")
        target_compile_definitions(EOS PRIVATE NOMINMAX)
        target_compile_definitions(EOS PRIVATE WIN32_LEAN_AND_MEAN)
        target_compile_definitions(EOS PUBLIC EOS_PLATFORM_WINDOWS)
        set(USE_WINDOWS TRUE)
    elseif(UNIX AND NOT APPLE)
        if(USE_WAYLAND)
            message(STATUS "Wayland session detected")
            target_compile_definitions(EOS PUBLIC EOS_PLATFORM_WAYLAND)
        elseif(USE_X11)
            message(STATUS "X11 session detected")
            target_compile_definitions(EOS PUBLIC EOS_PLATFORM_X11)
            SETUP_X11_NONE_WORKAROUND(EOS)
        else()
            # Fallback: detect from environment
            if(DEFINED ENV{WAYLAND_DISPLAY})
                message(STATUS "Wayland session detected (env)")
                target_compile_definitions(EOS PUBLIC EOS_PLATFORM_WAYLAND)
                set(USE_WAYLAND TRUE)
            else()
                message(STATUS "X11 session detected (env)")
                target_compile_definitions(EOS PUBLIC EOS_PLATFORM_X11)
                set(USE_X11 TRUE)
                SETUP_X11_NONE_WORKAROUND(EOS)
            endif()
        endif()
    endif()
endmacro()