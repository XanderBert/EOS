#pragma once
#include <slang-com-ptr.h>
#include <filesystem>
#include "defines.h"


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

    void CompileShaders(const ShaderCompilationDescription& shaderCompilationDescription);

private:
    Slang::ComPtr<slang::IGlobalSession> GlobalSession;
    Slang::ComPtr<slang::ISession> Session;
    Slang::ComPtr<slang::IBlob> Diagnostics;


    std::filesystem::path ShaderFolder;
};