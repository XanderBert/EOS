#include "shaderUtils.h"
#include <fstream>

#include "logger.h"
#include "utils.h"

namespace EOS
{
    using namespace slang;

    //https://shader-slang.org/slang/user-guide/compiling.html#using-the-compilation-api
    ShaderCompiler::ShaderCompiler(const std::filesystem::path& shaderFolder)
    : ShaderFolder(shaderFolder)
    {
        //Create a Global Session, This is not threadsafe, so if we want to multithread shader compilation we need to create a compiler for each thread
        SlangGlobalSessionDesc sessionDescription = {};
        SLANG_ASSERT_VOID_ON_FAIL(createGlobalSession(&sessionDescription, GlobalSession.writeRef()));
    }

    void ShaderCompiler::CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, ShaderInfo& outShaderInfo)
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
            EOS::Logger->critical("Slang Shader Compiler Diagnostics:\n{}", static_cast<const char *>(Diagnostics->getBufferPointer()));
            CHECK(false, "Shader Compile Error");
            Diagnostics.setNull();
        }


        Slang::ComPtr<IEntryPoint> entryPoint;
        module->findEntryPointByName(shaderCompilationDescription.EntryPoint, entryPoint.writeRef());
        if (!entryPoint)
        {
            EOS::Logger->error("Cannot find the shader  entry point: '{}' for shader: '{}'.", shaderCompilationDescription.EntryPoint, shaderCompilationDescription.Name);
        }

        IComponentType* components[] = { module, entryPoint };
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

        if (kernelBlob)
        {
            const void* bufferPtr = kernelBlob->getBufferPointer();
            size_t bufferSizeInBytes = kernelBlob->getBufferSize();

            CHECK(bufferSizeInBytes !=0, "Kernel blob is empty for shader '{}'.", shaderCompilationDescription.Name);
            CHECK(bufferSizeInBytes % sizeof(uint32_t) == 0, "Kernel blob size ({}) for shader '{}' is not a multiple of sizeof(uint32_t). SPIR-V data may be corrupt or invalid.", bufferSizeInBytes, shaderCompilationDescription.Name);

            const uint32_t* spirvWordData = static_cast<const uint32_t*>(bufferPtr);
            size_t spirvElementCount = bufferSizeInBytes / sizeof(uint32_t);

            // Populate outShaderInfo.spirv
            outShaderInfo.Spirv.clear();
            outShaderInfo.Spirv.assign(spirvWordData, spirvWordData + spirvElementCount);

            // Write the SPIR-V to disk if requested
            if (shaderCompilationDescription.WriteToDisk)
            {
                std::string baseName = shaderCompilationDescription.Name;

                // Check if name ends with .slang if so remove it for writing to disk
                const std::string extensionToRemove = ".slang";
                if (baseName.length() >= extensionToRemove.length() && baseName.rfind(extensionToRemove) == (baseName.length() - extensionToRemove.length()))
                {
                    baseName.erase(baseName.length() - extensionToRemove.length());
                }

                // Construct the full output path
                std::filesystem::path outputDir = path;
                std::filesystem::path outputPath = outputDir / (baseName + ".spirv");

                // create a std::string that contains the raw binary data from outShaderInfo.spirv.
                const char* rawDataBytes = reinterpret_cast<const char*>(outShaderInfo.Spirv.data());
                size_t rawDataSizeBytes = outShaderInfo.Spirv.size() * sizeof(uint32_t);
                std::string contentToWrite(rawDataBytes, rawDataSizeBytes);

                //Write to disk
                EOS::WriteFile(outputPath, contentToWrite);
            }
        }


        slang::ProgramLayout* reflection = linkedProgram->getLayout();
        slang::EntryPointReflection* entryPointLayout = reflection->getEntryPointByIndex(0);

        int totalPushConstantSize = 0;
        for (int i{}; i < reflection->getParameterCount(); ++i)
        {
            VariableLayoutReflection* variableLayout = reflection->getParameterByIndex(i);
            auto category = variableLayout->getCategory();

            // Whenever our pushconstant holds a struct of data
            if ( category == slang::PushConstantBuffer)
            {
                EOS::Logger->debug("Found Shader Variable: {}, of Type: {}, as PushConstant", variableLayout->getName(), variableLayout->getType()->getName());
                totalPushConstantSize += variableLayout->getTypeLayout()->getElementVarLayout()->getTypeLayout()->getSize();
            }

            // Whenever a "raw" datatype is used as pushconstant
            // TODO: i dont see a way at the moment how i can figure out whenever the uniform is used for push const or not.
            if (category == slang::Uniform)
            {
                EOS::Logger->debug("Found Shader Variable: {}, of Type: {}, as PushConstant", variableLayout->getName(), variableLayout->getType()->getName());
                assert(false);
                //totalPushConstantSize += variableLayout->getTypeLayout()->getSize();
            }
        }

        outShaderInfo.PushConstantSize = totalPushConstantSize;
        outShaderInfo.DebugName = shaderCompilationDescription.Name;
        outShaderInfo.ShaderStage = ToShaderStage(entryPointLayout);
    }

    EOS::ShaderStage ShaderCompiler::ToShaderStage(slang::EntryPointReflection *entryPointReflection)
    {
        switch (SlangStage slangStage = entryPointReflection->getStage())
        {
            case SLANG_STAGE_NONE:
                return ShaderStage::None;
            case SLANG_STAGE_VERTEX:
                return ShaderStage::Vertex;
            case SLANG_STAGE_HULL:
                return ShaderStage::Hull;
            case SLANG_STAGE_DOMAIN:
                return ShaderStage::Domain;
            case SLANG_STAGE_GEOMETRY:
                return ShaderStage::Geometry;
            case SLANG_STAGE_FRAGMENT:
                return ShaderStage::Fragment;
            case SLANG_STAGE_COMPUTE:
                return ShaderStage::Compute;
            case SLANG_STAGE_RAY_GENERATION:
                return ShaderStage::RayGen;
            case SLANG_STAGE_INTERSECTION:
                return ShaderStage::Intersection;
            case SLANG_STAGE_ANY_HIT:
                return ShaderStage::AnyHit;
            case SLANG_STAGE_CLOSEST_HIT:
                return ShaderStage::ClosestHit;
            case SLANG_STAGE_MISS:
                return ShaderStage::Miss;
            case SLANG_STAGE_CALLABLE:
                return ShaderStage::Callable;
            case SLANG_STAGE_MESH:
                return ShaderStage::Mesh;
            case SLANG_STAGE_AMPLIFICATION:
                return ShaderStage::Amplification;
            default:
                return ShaderStage::None;
        }
    }


    EOS::Holder<EOS::ShaderModuleHandle> LoadShader(const std::unique_ptr<EOS::IContext>& context, const std::unique_ptr<EOS::ShaderCompiler>& shaderCompiler, const char* fileName)
    {
        ShaderInfo shaderInfo{};
        shaderCompiler->CompileShader({fileName}, shaderInfo);

        return context->CreateShaderModule(shaderInfo);
    }
}