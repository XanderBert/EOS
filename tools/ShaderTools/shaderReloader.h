#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "shaderTypes.h"

namespace EOS
{
    class ShaderCompiler;
}

/**
 * @brief Watches the source files of every shader program in use, imports included, and recompiles a program when any
 *        of them changes. Each changed program is compiled once and handed to a callback that swaps it into whatever
 *        uses it.
 *
 * A program is watched while at least one user (a shader program handle or a pipeline) tracks it.
 * Without EOS_SHADER_TOOLS every member is a no-op.
 */
class ShaderReloader final
{
public:
    // Receives a successfully recompiled program; returns the number of pipelines it rebuilt.
    using ProgramRecompiledCallback = std::function<uint32_t(const std::shared_ptr<const EOS::CompiledShaderProgram>&)>;

    void TrackProgram(const EOS::CompiledShaderProgram& program);
    void UntrackProgram(const EOS::ShaderProgramDescription& description);

    /**
     * @brief Recompiles every tracked program whose sources changed since it was last compiled.
     * @return The number of pipelines rebuilt.
     */
    [[nodiscard]] uint32_t ReloadChangedShaders(EOS::ShaderCompiler& compiler, const ProgramRecompiledCallback& onProgramRecompiled);

private:
#if defined(EOS_SHADER_TOOLS)
    struct WatchedFile final
    {
        std::filesystem::path Path;
        std::filesystem::file_time_type LastWriteTime{};
    };

    struct TrackedProgram final
    {
        EOS::ShaderProgramDescription Description;
        std::vector<WatchedFile> Files{};
        uint32_t UseCount = 0;
    };

    static void WatchSources(TrackedProgram& trackedProgram, const EOS::CompiledShaderProgram& program);
    [[nodiscard]] static bool HaveSourcesChanged(const TrackedProgram& trackedProgram);

    std::unordered_map<uint64_t, TrackedProgram> Programs{};
#endif
};
