if(EOS_SHADER_TOOLS)
    # No linker depfile: build directories from before the tool stopped linking EOS still have EOS recorded as a
    # dependency of the tool, which Ninja would report as a cycle (EOS now depends on the tool). Its link inputs are
    # all CMake targets anyway, so the depfile adds nothing.
    set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)
    add_executable(EOSShaderCompilerTool ShaderTools/shader_compiler_tool.cpp)
    set_property(TARGET EOSShaderCompilerTool PROPERTY FOLDER "Internal/Tools")

    set_target_properties(EOSShaderCompilerTool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )

    SETUP_TARGET_DEFAULTS(EOSShaderCompilerTool)
    target_include_directories(EOSShaderCompilerTool PRIVATE "${CMAKE_SOURCE_DIR}/tools")
    # Not EOS: the tool generates headers EOS includes, so it is built and run before EOS.
    target_link_libraries(EOSShaderCompilerTool PRIVATE EOSShaderCompiler)
    eos_require_slang(EOSShaderCompilerTool)
    eos_copy_slang_runtime(EOSShaderCompilerTool)
endif()
