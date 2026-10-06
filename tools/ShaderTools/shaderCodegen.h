#pragma once
#if defined(EOS_SHADER_TOOLS)
#include <slang-include.h>
#include <string>
#include <vector>

#include "shaderCompiler.h"

// Generates C++ mirrors of the Slang structs a module marks with [CppExport] (declared in eos/core.slang), so data
// shared between C++ and shaders is declared once, in Slang.
//
// The generated structs have Slang's field names and scalar layout, which is how EOS lays out shader buffers,
// buffer pointers and push constants. Every size and offset is checked with a static_assert, so the C++ build fails
// instead of the GPU reading the wrong bytes if the two ever disagree.
//
// Slang                      C++
// float, int, uint, ...      float, int32_t, uint32_t, ...
// float3, int2, ...          glm::vec3, glm::ivec2, ...
// floatRxC                   glm::matRxC (same memory as Slang's row-major matrices: shaders compute mul(v, M))
// T[N]                       std::array<T, N>
// T*                         uint64_t (a buffer device address)
// DescriptorHandle<T>        EOS::DescriptorHandle
// enum                       enum class; every enum of a module that exports structs is exported with them
// struct                     the struct's own mirror, which must be [CppExport] as well
//
// Fields start zeroed, or at their Slang default when that is a literal number. bool (4 bytes in a buffer, 1 in
// C++), half, resources, generics and any default C++ cannot be given are reported as errors.
namespace EOS::ShaderCodegen
{
    /**
     * @brief Generates the header text for module.
     * @param session The session module was loaded in.
     * @param module The module to mirror.
     * @param moduleName module's name as used by `import`.
     * @param roots Shader directories with the namespace their modules' types go in. module has to be under one of them.
     * @param outHeader Receives the header, or nothing when the module has no [CppExport] struct.
     * @param outDiagnostics Receives every reason a struct cannot be mirrored.
     */
    [[nodiscard]] bool GenerateHeader(slang::ISession* session, slang::IModule* module, const std::string& moduleName, const std::vector<CppExportRoot>& roots, std::string& outHeader, std::string& outDiagnostics);
}
#endif
