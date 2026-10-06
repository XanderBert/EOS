#pragma once
#if defined(EOS_SHADER_TOOLS)
#include <slang-include.h>
#include <string>

#include "shaderTypes.h"

// Translates Slang's reflection of a linked program into EOS's Slang-free reflection types.
namespace EOS::SlangReflection
{
    /**
     * @brief Fills the reflection of program and of each of its entry points (everything except the SPIR-V).
     * @param linkedProgram A linked program whose entry points are, in order, the ones in program.EntryPoints.
     * @param program Must already have one ShaderEntryPoint per entry point of linkedProgram.
     * @param outDiagnostics Receives an explanation for every construct EOS cannot bind.
     * @return False when the program uses something EOS has no place for in its pipeline layout.
     */
    [[nodiscard]] bool Reflect(slang::IComponentType* linkedProgram, CompiledShaderProgram& program, std::string& outDiagnostics);

    [[nodiscard]] ShaderStage ToShaderStage(SlangStage stage);
}
#endif
