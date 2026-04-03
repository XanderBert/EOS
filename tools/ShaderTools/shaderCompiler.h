#pragma once
#if defined(EOS_SHADER_TOOLS)
#include <slang-include.h>
#endif
#include <filesystem>
#include <string>
#include <vector>

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
        explicit ShaderCompiler(const std::filesystem::path& outputFolder, const std::vector<std::string>& shaderSearchPaths = {});
        DELETE_COPY_MOVE(ShaderCompiler);

        [[nodiscard]] bool CompileAndCacheShader(const char* fileName);
        [[nodiscard]] bool LoadShader(const char* fileName, EOS::ShaderStage shaderStage, ShaderInfo& outShaderInfo, bool invalidate = false);
        static inline const char* ShaderFileFormat = ".EOS";

#if defined(EOS_SHADER_TOOLS)
    private:

        static EOS::ShaderStage ToShaderStage(SlangStage slangStage);

        [[nodiscard]] bool CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, std::vector<ShaderInfo>& outShaderInfo);
        void CacheShader(const ShaderCompilationDescription& shaderCompilationDescription, const ShaderInfo& info) const;
        [[nodiscard]] bool HandleEntryPoint(ShaderInfo& outShaderInfo, slang::IModule* module, const char* shaderName, SlangInt32 entryPointIndex);

        Slang::ComPtr<slang::IGlobalSession> GlobalSession;
        Slang::ComPtr<slang::ISession> Session;
        Slang::ComPtr<ISlangBlob> Diagnostics;
#endif

        static std::string ShaderStageToString(EOS::ShaderStage shaderStage);
        static void LoadShaderFromCache(const std::filesystem::path& path, ShaderInfo& outInfo);
        std::filesystem::path OutputFolder;
        std::vector<std::string> ShaderSearchPaths;
    };
}
