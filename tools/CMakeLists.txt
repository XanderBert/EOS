if(EOS_BUILD_SHADER_TOOLS AND EOS_SHADER_COMPILER)
    add_executable(EOSShaderCompilerTool shader_compiler_tool.cpp)
    set_property(TARGET EOSShaderCompilerTool PROPERTY FOLDER "Internal/Tools")

    set_target_properties(EOSShaderCompilerTool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )

    SETUP_TARGET_DEFAULTS(EOSShaderCompilerTool)
    target_link_libraries(EOSShaderCompilerTool PRIVATE EOS)
    eos_require_slang(EOSShaderCompilerTool)
endif()
