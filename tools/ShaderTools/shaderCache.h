#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "shaderTypes.h"

// On-disk cache of compiled shader programs.
// One file per program, holding the SPIR-V of every entry point together with the reflection and the source files
// it was compiled from. The file is only valid for the exact compiler configuration it was written with, which is
// captured as an options hash in the header.
namespace EOS::ShaderCache
{
    inline constexpr const char* FileExtension = ".EOS";

    [[nodiscard]] uint64_t HashBytes(const void* data, size_t size, uint64_t seed);
    [[nodiscard]] uint64_t HashString(std::string_view text, uint64_t seed);
    [[nodiscard]] uint64_t HashDescription(const ShaderProgramDescription& description);
    [[nodiscard]] bool HashFile(const std::filesystem::path& path, uint64_t& outHash);

    // Programs compiled with the default entry points and no defines are stored as "<module>.EOS", so the cache
    // directory stays readable. Anything else gets the description hash appended.
    [[nodiscard]] std::filesystem::path GetFilePath(const std::filesystem::path& cacheDirectory, const ShaderProgramDescription& description);

    [[nodiscard]] bool Write(const std::filesystem::path& path, uint64_t optionsHash, const CompiledShaderProgram& program, std::string& outError);

    // Fails when the file is missing, corrupt, written by another cache format, compiled with other options or for
    // another program description. outError says which.
    [[nodiscard]] bool Read(const std::filesystem::path& path, uint64_t optionsHash, const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError);

    // True when every source file the program was compiled from still exists with the same contents.
    [[nodiscard]] bool AreDependenciesUpToDate(const CompiledShaderProgram& program);
}
