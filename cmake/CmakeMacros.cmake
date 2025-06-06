macro(SETUP_GROUPS src_files)
    foreach(FILE ${src_files})
        get_filename_component(PARENT_DIR "${FILE}" PATH)

        # skip src or include and changes /'s to \\'s
        set(GROUP "${PARENT_DIR}")
        string(REPLACE "." "\\" GROUP "${GROUP}")

        source_group("${GROUP}" FILES "${FILE}")
    endforeach()
endmacro()


macro(CREATE_APP name)
    set(PROJECT_NAME ${name})
    project(${PROJECT_NAME} CXX)

    file(GLOB_RECURSE SRC_FILES LIST_DIRECTORIES false src/*.c??)
    file(GLOB_RECURSE HEADER_FILES LIST_DIRECTORIES false src/*.h)
    file(GLOB_RECURSE SHADER_FILES LIST_DIRECTORIES false src/*.slang)

    message(STATUS "SRC_FILES: ${SRC_FILES}")
    message(STATUS "HEADER_FILES: ${HEADER_FILES}")
    message(STATUS "SHADER_FILES: ${SHADER_FILES}")

    include_directories(${CMAKE_SOURCE_DIR}/src)
    add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES} ${SHADER_FILES})

    SETUP_GROUPS("${SRC_FILES}")
    SETUP_GROUPS("${HEADER_FILES}")
    SOURCE_GROUP(shaders FILES "${SHADER_FILES}")

    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_DEBUG ${PROJECT_NAME}_Debug)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_RELEASE ${PROJECT_NAME}_Release)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_RELWITHDEBINFO ${PROJECT_NAME}_ReleaseDebInfo)

    # On Linux the binaries are stored in the bin folder
    if (UNIX)
        set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
    endif()

    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lstdc++")

    if(MSVC)
        add_definitions(-D_CONSOLE)
        set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()

    # Copy shaders
    foreach(SHADER ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        add_custom_command(
                TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SHADER}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${SHADER_NAME}"
                COMMENT "Copying shader file: ${SHADER_NAME}"
        )
    endforeach()
endmacro()