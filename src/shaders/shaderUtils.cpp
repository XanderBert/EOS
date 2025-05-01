#include "shaderUtils.h"
#include <fstream>

#include "logger.h"

//https://shader-slang.org/slang/user-guide/compiling.html#using-the-compilation-api
using namespace slang;
ShaderCompiler::ShaderCompiler(const std::filesystem::path& shaderFolder)
: ShaderFolder(shaderFolder)
{
    //Create a Global Session, This is not threadsafe, so if we want to multithread shader compilation we need to create a compiler for each thread
    SlangGlobalSessionDesc sessionDescription = {};
    SLANG_ASSERT_VOID_ON_FAIL(createGlobalSession(&sessionDescription, GlobalSession.writeRef()));
}

void ShaderCompiler::CompileShaders(const ShaderCompilationDescription& shaderCompilationDescription)
{
    TargetDesc targetDesc
    {
#if defined(EOS_VULKAN)
    .format = SLANG_SPIRV,
#elif defined(EOS_DIRECTX)
    .format = SLANG_DXBC
#endif
    .profile = GlobalSession->findProfile("glsl_450"),
    };

    const char* path = ShaderFolder.string().c_str();
    SessionDesc sessionDesc
    {
        .targets = &targetDesc,
        .targetCount = 1,
        .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR,
        .searchPaths = &path,
        .searchPathCount = 1,
    };

    GlobalSession->createSession(sessionDesc, Session.writeRef());

    // Will load and compile it (if it hasn’t been done already).
    IModule *module = Session->loadModule(shaderCompilationDescription.Name, Diagnostics.writeRef());
    if(Diagnostics)
    {
        EOS::Logger->warn("Slang Shader Compiler: {}", Diagnostics->getBufferPointer());
    }
    Diagnostics.setNull();

    Slang::ComPtr<IEntryPoint> computeEntryPoint;
    module->findEntryPointByName(shaderCompilationDescription.EntryPoint, computeEntryPoint.writeRef());

    IComponentType* components[] = { module, computeEntryPoint };
    Slang::ComPtr<IComponentType> program;
    Session->createCompositeComponentType(components, 2, program.writeRef());

    slang::ProgramLayout* layout = program->getLayout();

    // resolve all cross-module references
    // also used to resolve link time specializaions (https://shader-slang.org/slang/user-guide/link-time-specialization)
    Slang::ComPtr<IComponentType> linkedProgram;
    Slang::ComPtr<ISlangBlob> diagnosticBlob;
    program->link(linkedProgram.writeRef(), diagnosticBlob.writeRef());

    constexpr int entryPointIndex = 0;    // only one entry point
    constexpr int targetIndex = 0;        // only one target
    Slang::ComPtr<ISlangBlob> kernelBlob;
    linkedProgram->getEntryPointCode(entryPointIndex, targetIndex, kernelBlob.writeRef(), Diagnostics.writeRef());
    Diagnostics.setNull();

    //TODO: return reflection information
    slang::ProgramLayout* reflection = linkedProgram->getLayout();

    if (kernelBlob)
    {
        std::vector<uint32_t> spirv;

        const uint32_t* code = static_cast<const uint32_t *>(kernelBlob->getBufferPointer());
        size_t codeSize = kernelBlob->getBufferSize() / sizeof(uint32_t);
        spirv.resize(codeSize);
        spirv.assign(code, code + codeSize);

        //Write the spriv to disk if wanted
        if (shaderCompilationDescription.WriteToDisk)
        {
            std::ofstream out( fmt::format("{}/{}.spirv", path, shaderCompilationDescription.Name), std::ios::binary);
            out.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
        }
    }
}