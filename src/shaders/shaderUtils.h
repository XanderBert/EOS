#pragma once

#pragma push_macro("None")
#undef None  // Undefine X11's 'None' macro
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#pragma pop_macro("None") // Restore the 'None' macro for X11

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