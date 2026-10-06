#include "shaderReloader.h"

#include <system_error>

#include "logger.h"
#include "shaderCache.h"
#include "shaderCompiler.h"

void ShaderReloader::TrackProgram([[maybe_unused]] const EOS::CompiledShaderProgram& program)
{
#if defined(EOS_SHADER_TOOLS)
    auto [it, isNewProgram] = Programs.try_emplace(EOS::ShaderCache::HashDescription(program.Description));
    TrackedProgram& trackedProgram = it->second;
    if (isNewProgram)
    {
        trackedProgram.Description = program.Description;
        WatchSources(trackedProgram, program);
    }

    ++trackedProgram.UseCount;
#endif
}

void ShaderReloader::UntrackProgram([[maybe_unused]] const EOS::ShaderProgramDescription& description)
{
#if defined(EOS_SHADER_TOOLS)
    const auto it = Programs.find(EOS::ShaderCache::HashDescription(description));
    if (it == Programs.end()) return;

    if (--it->second.UseCount == 0) Programs.erase(it);
#endif
}

uint32_t ShaderReloader::ReloadChangedShaders([[maybe_unused]] EOS::ShaderCompiler& compiler, [[maybe_unused]] const ProgramRecompiledCallback& onProgramRecompiled)
{
#if defined(EOS_SHADER_TOOLS)
    std::vector<TrackedProgram*> changedPrograms;
    for (auto& [programKey, trackedProgram] : Programs)
    {
        if (HaveSourcesChanged(trackedProgram)) changedPrograms.push_back(&trackedProgram);
    }

    if (changedPrograms.empty()) return 0;

    // Slang caches every module it has loaded; start over so edited imports are read from disk again.
    compiler.ResetModuleCache();

    uint32_t numberOfRebuiltPipelines = 0;
    for (TrackedProgram* trackedProgram : changedPrograms)
    {
        const std::string& moduleName = trackedProgram->Description.Module;

        std::string diagnostics;
        const std::shared_ptr<const EOS::CompiledShaderProgram> program = compiler.CompileProgram(trackedProgram->Description, diagnostics);
        if (!program)
        {
            EOS::Logger->error("Hot reload: '{}' failed to compile, keeping the previous version.\n{}", moduleName, diagnostics);

            // Remember the failed version so it is not recompiled again until it changes.
            for (WatchedFile& file : trackedProgram->Files)
            {
                std::error_code errorCode;
                file.LastWriteTime = std::filesystem::last_write_time(file.Path, errorCode);
            }
            continue;
        }

        if (!diagnostics.empty()) EOS::Logger->warn("Hot reload: '{}'\n{}", moduleName, diagnostics);

        WatchSources(*trackedProgram, *program);
        numberOfRebuiltPipelines += onProgramRecompiled(program);
        EOS::Logger->info("Hot reload: recompiled '{}'", moduleName);
    }

    if (numberOfRebuiltPipelines > 0) EOS::Logger->info("Hot reload: rebuilt {} pipelines", numberOfRebuiltPipelines);
    return numberOfRebuiltPipelines;
#else
    return 0;
#endif
}

#if defined(EOS_SHADER_TOOLS)
void ShaderReloader::WatchSources(TrackedProgram& trackedProgram, const EOS::CompiledShaderProgram& program)
{
    trackedProgram.Files.clear();
    for (const EOS::ShaderSourceDependency& dependency : program.Dependencies)
    {
        std::error_code errorCode;
        trackedProgram.Files.push_back({.Path = dependency.Path, .LastWriteTime = std::filesystem::last_write_time(dependency.Path, errorCode)});
    }
}

bool ShaderReloader::HaveSourcesChanged(const TrackedProgram& trackedProgram)
{
    for (const WatchedFile& file : trackedProgram.Files)
    {
        // Any difference counts, not only newer: checking out an older revision also changes the shader.
        std::error_code errorCode;
        if (std::filesystem::last_write_time(file.Path, errorCode) != file.LastWriteTime) return true;
    }

    return false;
}
#endif
