#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <EOS.h>

class ShaderReloader final
{
public:
    using ReloadShaderModuleCallback = std::function<bool(EOS::ShaderModuleHandle, const char*, EOS::ShaderStage)>;
    using RebuildRenderPipelineCallback = std::function<bool(EOS::RenderPipelineHandle)>;
    using RebuildComputePipelineCallback = std::function<bool(EOS::ComputePipelineHandle)>;

    explicit ShaderReloader(std::filesystem::path shaderSourcePath, std::filesystem::path engineShaderSourcePath = {});

    void TrackShader(const EOS::ShaderModuleHandle& shaderHandle, const char* fileName, EOS::ShaderStage shaderStage);
    void UntrackShader(const EOS::ShaderModuleHandle& shaderHandle);

    void RegisterRenderPipelineDependencies(const EOS::RenderPipelineHandle& pipelineHandle, const EOS::RenderPipelineDescription& renderPipelineDescription);
    void UnregisterRenderPipelineDependencies(const EOS::RenderPipelineHandle& pipelineHandle);

    void RegisterComputePipelineDependencies(const EOS::ComputePipelineHandle& pipelineHandle, const EOS::ComputePipelineDescription& computePipelineDescription);
    void UnregisterComputePipelineDependencies(const EOS::ComputePipelineHandle& pipelineHandle);

    [[nodiscard]] uint32_t ReloadChangedShaders(const ReloadShaderModuleCallback& reloadShaderModuleCallback, const RebuildRenderPipelineCallback& rebuildRenderPipelineCallback, const RebuildComputePipelineCallback& rebuildComputePipelineCallback = nullptr);

private:
#if defined(EOS_SHADER_TOOLS)
    struct ShaderHandleLess final
    {
        [[nodiscard]] bool operator()(const EOS::ShaderModuleHandle& lhs, const EOS::ShaderModuleHandle& rhs) const
        {
            if (lhs.Index() != rhs.Index())
            {
                return lhs.Index() < rhs.Index();
            }

            return lhs.Gen() < rhs.Gen();
        }
    };

    struct TrackedShader final
    {
        std::filesystem::path SourceFilePath{};
        std::string ModuleName{};
        EOS::ShaderStage ShaderStage = EOS::ShaderStage::None;
        std::filesystem::file_time_type LastWriteTime{};
        bool MissingTimestampWarningLogged = false;
        std::vector<EOS::RenderPipelineHandle> DependentRenderPipelines{};
        std::vector<EOS::ComputePipelineHandle> DependentComputePipelines{};
    };

    static void AddUniqueRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle);
    static void RemoveRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle);
    static void AddUniqueComputePipelineHandle(std::vector<EOS::ComputePipelineHandle>& handles, EOS::ComputePipelineHandle handle);
    static void RemoveComputePipelineHandle(std::vector<EOS::ComputePipelineHandle>& handles, EOS::ComputePipelineHandle handle);

    std::map<EOS::ShaderModuleHandle, TrackedShader, ShaderHandleLess> ShaderMap{};
#endif

    std::filesystem::path ShaderSourcePath{};
    std::filesystem::path EngineShaderSourcePath{};
};
