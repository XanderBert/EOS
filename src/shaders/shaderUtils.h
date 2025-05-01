#pragma once

//X11 has some things defined that Slang uses in enums, here we fix those name collisions
#pragma push_macro("None")
#undef None
#pragma push_macro("Bool")
#undef Bool
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#pragma pop_macro("Bool")
#pragma pop_macro("None")

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
    Slang::ComPtr<ISlangBlob> Diagnostics;


    std::filesystem::path ShaderFolder;
};