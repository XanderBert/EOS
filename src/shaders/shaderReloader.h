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

    explicit ShaderReloader(std::filesystem::path shaderSourcePath);

    void TrackShader(const EOS::ShaderModuleHandle& shaderHandle, const char* fileName, EOS::ShaderStage shaderStage);
    void UntrackShader(const EOS::ShaderModuleHandle& shaderHandle);

    void RegisterRenderPipelineDependencies(const EOS::RenderPipelineHandle& pipelineHandle, const EOS::RenderPipelineDescription& renderPipelineDescription);
    void UnregisterRenderPipelineDependencies(const EOS::RenderPipelineHandle& pipelineHandle);

    [[nodiscard]] uint32_t ReloadChangedShaders(const ReloadShaderModuleCallback& reloadShaderModuleCallback, const RebuildRenderPipelineCallback& rebuildRenderPipelineCallback);

private:
#if defined(EOS_ENABLE_SHADER_RELOADER)
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
        std::string FileName{};
        EOS::ShaderStage ShaderStage = EOS::ShaderStage::None;
        std::filesystem::file_time_type LastWriteTime{};
        bool MissingTimestampWarningLogged = false;
        std::vector<EOS::RenderPipelineHandle> DependentRenderPipelines{};
    };

    static void AddUniqueRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle);
    static void RemoveRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle);

    std::map<EOS::ShaderModuleHandle, TrackedShader, ShaderHandleLess> ShaderMap{};
#endif

    std::filesystem::path ShaderSourcePath{};
};
