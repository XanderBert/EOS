#pragma once
#include <slang-include.h>
#include <filesystem>

#include "defines.h"
#include "EOS.h"


namespace EOS
{
    struct ShaderCompilationDescription
    {
        const char* Name;
        const char* EntryPoint = "main";
        bool WriteToDisk = true;
    };

    class ShaderCompiler
    {
    public:
        explicit ShaderCompiler(const std::filesystem::path& shaderFolder);
        DELETE_COPY_MOVE(ShaderCompiler);

        void CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, ShaderInfo& outShaderInfo);

    private:
        static EOS::ShaderStage ToShaderStage(slang::EntryPointReflection* entryPointReflection);

        Slang::ComPtr<slang::IGlobalSession> GlobalSession;
        Slang::ComPtr<slang::ISession> Session;
        Slang::ComPtr<ISlangBlob> Diagnostics;

        std::filesystem::path ShaderFolder;
    };


    /**
    * @brief Compiles the shader if needed and returns a Holder handle to the compiled shader
    * @param
    * @param
    * @param 
    * @return a Holder Handle to the compiled shader
    */
    Holder<ShaderModuleHandle> LoadShader(const std::unique_ptr<EOS::IContext>& context, const std::unique_ptr<EOS::ShaderCompiler>& shaderCompiler, const char* fileName);

}
