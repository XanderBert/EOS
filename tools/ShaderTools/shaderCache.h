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

    // Programs compiled with the default entry points, no defines and no linked modules are stored as "<module>.EOS", so
    // the cache directory stays readable. Anything else gets the description hash appended.
    [[nodiscard]] std::filesystem::path GetFilePath(const std::filesystem::path& cacheDirectory, const ShaderProgramDescription& description);

    [[nodiscard]] bool Write(const std::filesystem::path& path, uint64_t optionsHash, const CompiledShaderProgram& program, std::string& outError);

    // Fails when the file is missing, corrupt, written by another cache format, compiled with other options or for
    // another program description. outError says which.
    [[nodiscard]] bool Read(const std::filesystem::path& path, uint64_t optionsHash, const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError);

    // True when every source file the program was compiled from still exists with the same contents.
    [[nodiscard]] bool AreDependenciesUpToDate(const CompiledShaderProgram& program);
    [[nodiscard]] bool AreDependenciesUpToDate(const std::vector<ShaderSourceDependency>& dependencies);

    /**
     * @brief Something else derived from shader sources (a precompiled module, a generated header) and what it was made
     *        from, so it is reused until one of those changes.
     */
    struct DerivedEntry final
    {
        uint64_t Key = 0;                                   // everything besides the sources: options, tool and Slang version
        std::filesystem::path Source{};                     // the file it was made from
        std::vector<ShaderSourceDependency> Dependencies{};
        std::string Payload{};                              // stored as is
    };

    [[nodiscard]] bool WriteDerived(const std::filesystem::path& path, const DerivedEntry& entry, std::string& outError);

    // Fails when the file is missing, corrupt or was written with another key. Does not check the dependencies.
    [[nodiscard]] bool ReadDerived(const std::filesystem::path& path, uint64_t key, DerivedEntry& outEntry);
}
