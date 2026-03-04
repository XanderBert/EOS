#pragma once
#include <slang-include.h>
#include <filesystem>

#include "defines.h"
#include "EOS.h"


namespace EOS
{
    struct ShaderCompilationDescription final
    {
        const char* Name;
        bool Cache = true;
    };

    struct CachedShaderHeader final
    {
        uint32_t checksum = EOS_SHADER_CHECKSUM;
        uint32_t version = 1;
        ShaderStage stage;
        uint32_t pushConstantSize;
        uint32_t debugNameLength;
        uint32_t spirvSize;
    };

    class ShaderCompiler final
    {
    public:
        explicit ShaderCompiler(const std::filesystem::path& shaderFolder);
        DELETE_COPY_MOVE(ShaderCompiler);

        ShaderInfo LoadShader(const char* fileName, EOS::ShaderStage shaderStage, bool invalidate = true);
        static inline const char* ShaderFileFormat = ".EOS";
        
    private:

        static EOS::ShaderStage ToShaderStage(SlangStage slangStage);
        static std::string ShaderStageToString(EOS::ShaderStage shaderStage);

        void CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, std::vector<ShaderInfo>& outShaderInfo);
        static void LoadShaderFromCache(const std::filesystem::path& path, ShaderInfo& outInfo);
        void CacheShader(const ShaderCompilationDescription& shaderCompilationDescription, const ShaderInfo& info) const;

        void HandleEntryPoint(ShaderInfo& outShaderInfo, slang::IModule* module, const char* shaderName, SlangInt32 entryPointIndex);

        Slang::ComPtr<slang::IGlobalSession> GlobalSession;
        Slang::ComPtr<slang::ISession> Session;
        Slang::ComPtr<ISlangBlob> Diagnostics;

        std::filesystem::path ShaderFolder;
    };
}
