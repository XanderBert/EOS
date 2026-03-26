#include "shaderReloader.h"

#include <algorithm>
#include <array>

#include "logger.h"
#include "utils.h"

//TODO: I need to handle that fact that shaders get copied...

ShaderReloader::ShaderReloader(std::filesystem::path shaderSourcePath)
: ShaderSourcePath(std::move(shaderSourcePath))
{}

void ShaderReloader::TrackShader([[maybe_unused]] const EOS::ShaderModuleHandle& shaderHandle,[[maybe_unused]]  const char* fileName,[[maybe_unused]]  EOS::ShaderStage shaderStage)
{
#if defined(EOS_SHADER_COMPILER)
    if (!shaderHandle.Valid() || !fileName)  return;

    TrackedShader& trackedShader = ShaderMap[shaderHandle];
    trackedShader.FileName = fileName;
    trackedShader.ShaderStage = shaderStage;
    trackedShader.LastWriteTime = EOS::GetLastWriteTime(trackedShader.FileName.append(".slang"));
#endif
}

void ShaderReloader::UntrackShader([[maybe_unused]] const EOS::ShaderModuleHandle& shaderHandle)
{
#if defined(EOS_SHADER_COMPILER)
    ShaderMap.erase(shaderHandle);
#endif
}

void ShaderReloader::RegisterRenderPipelineDependencies([[maybe_unused]] const EOS::RenderPipelineHandle& pipelineHandle, [[maybe_unused]] const EOS::RenderPipelineDescription& renderPipelineDescription)
{
#if defined(EOS_SHADER_COMPILER)
    CHECK_RETURN(pipelineHandle.Valid(), "RenderPipeline is not valid!");

    const std::array<EOS::ShaderModuleHandle, 7> shaderHandles =
    {
        renderPipelineDescription.VertexShader,
        renderPipelineDescription.TessellationControlShader,
        renderPipelineDescription.TesselationShader,
        renderPipelineDescription.GeometryShader,
        renderPipelineDescription.FragmentShader,
        renderPipelineDescription.TaskShader,
        renderPipelineDescription.MeshShader,
    };

    for (const EOS::ShaderModuleHandle shaderHandle : shaderHandles)
    {
        if (!shaderHandle.Valid()) continue;

        TrackedShader& trackedShader = ShaderMap[shaderHandle];
        AddUniqueRenderPipelineHandle(trackedShader.DependentRenderPipelines, pipelineHandle);
    }
#endif
}

void ShaderReloader::UnregisterRenderPipelineDependencies([[maybe_unused]] const EOS::RenderPipelineHandle& pipelineHandle)
{
#if defined(EOS_SHADER_COMPILER)
    CHECK_RETURN(pipelineHandle.Valid(), "RenderPipeline is not valid!");

    for (auto& [shaderHandle, trackedShader] : ShaderMap)
    {
        static_cast<void>(shaderHandle);
        RemoveRenderPipelineHandle(trackedShader.DependentRenderPipelines, pipelineHandle);
    }
#endif
}

void ShaderReloader::RegisterComputePipelineDependencies([[maybe_unused]] const EOS::ComputePipelineHandle& pipelineHandle, [[maybe_unused]] const EOS::ComputePipelineDescription& computePipelineDescription)
{
#if defined(EOS_SHADER_COMPILER)
    CHECK_RETURN(pipelineHandle.Valid(), "ComputePipeline is not valid!");

    const EOS::ShaderModuleHandle computeShaderHandle = computePipelineDescription.ComputeShader;

    if (!computeShaderHandle.Valid()) return;

    TrackedShader& trackedShader = ShaderMap[computeShaderHandle];
    AddUniqueComputePipelineHandle(trackedShader.DependentComputePipelines, pipelineHandle);
#endif
}

void ShaderReloader::UnregisterComputePipelineDependencies([[maybe_unused]] const EOS::ComputePipelineHandle& pipelineHandle)
{
#if defined(EOS_SHADER_COMPILER)
    CHECK_RETURN(pipelineHandle.Valid(), "ComputePipeline is not valid!");

    for (auto& [shaderHandle, trackedShader] : ShaderMap)
    {
        static_cast<void>(shaderHandle);
        RemoveComputePipelineHandle(trackedShader.DependentComputePipelines, pipelineHandle);
    }
#endif
}

uint32_t ShaderReloader::ReloadChangedShaders([[maybe_unused]] const ReloadShaderModuleCallback& reloadShaderModuleCallback,[[maybe_unused]] const RebuildRenderPipelineCallback& rebuildRenderPipelineCallback, [[maybe_unused]] const RebuildComputePipelineCallback& rebuildComputePipelineCallback)
{
#if defined(EOS_SHADER_COMPILER)
    if (!reloadShaderModuleCallback || !rebuildRenderPipelineCallback) return 0;

    std::vector<EOS::RenderPipelineHandle> pipelinesToRebuild;
    std::vector<EOS::ComputePipelineHandle> computePipelinesToRebuild;
    for (auto& [shaderHandle, trackedShader] : ShaderMap)
    {
        if (!shaderHandle.Valid() || trackedShader.FileName.empty())
        {
            continue;
        }

        if (trackedShader.ShaderStage == EOS::ShaderStage::None)
        {
            continue;
        }

        const std::filesystem::file_time_type currentWriteTime = EOS::GetLastWriteTime(trackedShader.FileName);
        if (currentWriteTime == std::filesystem::file_time_type{})
        {
            if (!trackedShader.MissingTimestampWarningLogged)
            {
                EOS::Logger->warn("Hot reload could not resolve timestamp for shader source: {}", trackedShader.FileName);
                trackedShader.MissingTimestampWarningLogged = true;
            }
            continue;
        }

        trackedShader.MissingTimestampWarningLogged = false;

        if (trackedShader.LastWriteTime != std::filesystem::file_time_type{} && currentWriteTime <= trackedShader.LastWriteTime)
        {
            continue;
        }

        if (!reloadShaderModuleCallback(shaderHandle, trackedShader.FileName.c_str(), trackedShader.ShaderStage))
        {
            continue;
        }

        trackedShader.LastWriteTime = currentWriteTime;

        for (const EOS::RenderPipelineHandle pipelineHandle : trackedShader.DependentRenderPipelines)
        {
            AddUniqueRenderPipelineHandle(pipelinesToRebuild, pipelineHandle);
        }

        for (const EOS::ComputePipelineHandle computePipelineHandle : trackedShader.DependentComputePipelines)
        {
            AddUniqueComputePipelineHandle(computePipelinesToRebuild, computePipelineHandle);
        }
    }

    uint32_t numberOfRebuiltGraphicsPipelines = 0;
    for (const EOS::RenderPipelineHandle pipelineHandle : pipelinesToRebuild)
    {
        if (rebuildRenderPipelineCallback(pipelineHandle)) ++numberOfRebuiltGraphicsPipelines;
    }
    if (numberOfRebuiltGraphicsPipelines > 0) EOS::Logger->info("Rebuilt {} Graphics Pipelines after shader reload", numberOfRebuiltGraphicsPipelines);


    uint32_t numberOfRebuiltComputePipelines = 0;
    if (rebuildComputePipelineCallback)
    {
        for (const EOS::ComputePipelineHandle computePipelineHandle : computePipelinesToRebuild)
        {
            if (rebuildComputePipelineCallback(computePipelineHandle)) ++numberOfRebuiltComputePipelines;
        }
    }
    if (numberOfRebuiltComputePipelines > 0) EOS::Logger->info("Rebuilt {} Compute Pipelines after shader reload", numberOfRebuiltComputePipelines);



    return numberOfRebuiltGraphicsPipelines;
#endif
    return 0;
}

#if defined(EOS_SHADER_COMPILER)
void ShaderReloader::AddUniqueRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle)
{
    CHECK_RETURN(handle.Valid(), "The renderPipelineHandle is not valid!");
    for (const EOS::RenderPipelineHandle existingHandle : handles)
    {
        if (existingHandle == handle) return;
    }

    handles.push_back(handle);
}

void ShaderReloader::RemoveRenderPipelineHandle(std::vector<EOS::RenderPipelineHandle>& handles, EOS::RenderPipelineHandle handle)
{
    CHECK_RETURN(handle.Valid(), "The renderPipelineHandle is not valid!");
    if (handles.empty()) return;

    for (auto iterator = handles.begin(); iterator != handles.end();)
    {
        if (*iterator == handle)
        {
            iterator = handles.erase(iterator);
            continue;
        }

        ++iterator;
    }
}

void ShaderReloader::AddUniqueComputePipelineHandle(std::vector<EOS::ComputePipelineHandle>& handles, EOS::ComputePipelineHandle handle)
{
    CHECK_RETURN(handle.Valid(), "The computePipelineHandle is not valid!");
    for (const EOS::ComputePipelineHandle existingHandle : handles)
    {
        if (existingHandle == handle) return;
    }

    handles.push_back(handle);
}

void ShaderReloader::RemoveComputePipelineHandle(std::vector<EOS::ComputePipelineHandle>& handles, EOS::ComputePipelineHandle handle)
{
    CHECK_RETURN(handle.Valid(), "The computePipelineHandle is not valid!");
    if (handles.empty()) return;

    for (auto iterator = handles.begin(); iterator != handles.end();)
    {
        if (*iterator == handle)
        {
            iterator = handles.erase(iterator);
            continue;
        }

        ++iterator;
    }
}
#endif
