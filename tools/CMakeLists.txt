add_executable(EOSTextureCompressor texture_packer.cpp)
set_property(TARGET EOSTextureCompressor PROPERTY FOLDER "Internal/Tools")

set_target_properties(EOSTextureCompressor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

SETUP_TARGET_DEFAULTS(EOSTextureCompressor)
target_include_directories(EOSTextureCompressor PRIVATE "${CMAKE_SOURCE_DIR}/src")

# Keep the texture packer optimized even when the main workspace is configured as Debug.
target_compile_definitions(EOSTextureCompressor PRIVATE NDEBUG)
target_compile_options(EOSTextureCompressor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>
)

eos_require_stb(EOSTextureCompressor)
eos_require_ktx(EOSTextureCompressor)
