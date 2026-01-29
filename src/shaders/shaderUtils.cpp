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
        //Create a Global Session, This is not threadsafe, so if we want to multithread shader compilation we need to create a global session for each thread
        SlangGlobalSessionDesc sessionDescription = {};
        SLANG_ASSERT_VOID_ON_FAIL(createGlobalSession(&sessionDescription, GlobalSession.writeRef()));
    }

    void ShaderCompiler::CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, std::vector<ShaderInfo>& outShaderInfo)
    {
        const TargetDesc targetDesc
        {
#if defined(EOS_VULKAN)
        .format = SLANG_SPIRV,
#elif defined(EOS_DIRECTX)
        .format = SLANG_DXBC
#endif
        .profile = GlobalSession->findProfile("spirv_1_5"),
        };

        const char* path = ShaderFolder.string().c_str();
        const SessionDesc sessionDesc
        {
            .targets = &targetDesc,
            .targetCount = 1,
            .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR,
            .searchPaths = &path,
            .searchPathCount = 1,
        };
        GlobalSession->createSession(sessionDesc, Session.writeRef());

        // Will load the .slang file
        IModule* module = Session->loadModule(shaderCompilationDescription.Name, Diagnostics.writeRef());
        if(Diagnostics)
        {
            EOS::Logger->warn("Slang Shader Compiler Diagnostics:\n{}", static_cast<const char *>(Diagnostics->getBufferPointer()));
            Diagnostics.setNull();
        }


        // Load All Entry Points (a EntryPoint is a shader within a file) and write to disk if desired
        const uint32_t entryPointCount = module->getDefinedEntryPointCount();
        outShaderInfo.clear();
        outShaderInfo.resize(entryPointCount);
        for (SlangInt32 i{}; i < entryPointCount; ++i)
        {
            // Read out the data for this entry point and store it in the shader info.
            HandleEntryPoint(outShaderInfo[i], module, shaderCompilationDescription.Name, i);

            // Write the SPIR-V to disk if requested
            if (shaderCompilationDescription.WriteToDisk)
            {
                WriteShaderToDisk(shaderCompilationDescription, outShaderInfo[i]);
            }
        }
    }

    EOS::ShaderStage ShaderCompiler::ToShaderStage(SlangStage slangStage)
    {
        switch (slangStage)
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

    std::string ShaderCompiler::ShaderStageToString(EOS::ShaderStage shaderStage)
    {
        switch (shaderStage)
        {
        case ShaderStage::Amplification:
            return "Amplification";
        case ShaderStage::Vertex:
            return "Vertex";
        case ShaderStage::Hull:
            return "Hull";
        case ShaderStage::Domain:
            return "Domain";
        case ShaderStage::Geometry:
            return "Geometry";
        case ShaderStage::Fragment:
            return "Fragment";
        case ShaderStage::Compute:
            return "Compute";
        case ShaderStage::RayGen:
            return "RayGen";
        case ShaderStage::Intersection:
            return "Intersection";
        case ShaderStage::AnyHit:
            return "AnyHit";
        case ShaderStage::ClosestHit:
            return "ClosestHit";
        case ShaderStage::Miss:
            return "Miss";
        case ShaderStage::Callable:
            return "Callable";
        case ShaderStage::Mesh:
            return "Mesh";
        case ShaderStage::None:
            return "None";
        }

        return "None";
    }

    EOS::Holder<EOS::ShaderModuleHandle> LoadShader(const std::unique_ptr<EOS::IContext>& context, const std::unique_ptr<EOS::ShaderCompiler>& shaderCompiler, const char* fileName, const EOS::ShaderStage& shaderStage)
    {
        // First we check if the coresponding shaderfile and all of its entry points have been compiled yet,
        const std::filesystem::path cachedFilePath = fmt::format(".cache/{}{}{}", fileName, ShaderCompiler::ShaderStageToString(shaderStage),".spirv");

        std::ifstream file(cachedFilePath, std::ios::in | std::ios::binary);
        if (file.is_open())
        {
            file.close();
            EOS::Logger->info("{} was already compiled and cached", fileName);
            //TODO: return the binary
        }

        //Compile and write to disk
        std::vector<ShaderInfo> shaderInfos{};


        // TODO: shaderinfo can be concidered as POD
        // What if we wrote our shaderInfo to our own file together with the .spirv
        // Then it could be loaded in and shaderInfo could be red from that file.
        // Then we also don't need to pass a ref to a vector<ShaderInfo> it can just be written? or why would we, we can stil search for it in the vector
        shaderCompiler->CompileShader({fileName}, shaderInfos);

        // We could already return the correct shaderinfo instead of filling up a vector,
        // The vector doesn't make sense because we store the data in the file and only need 1.
        for (const auto& shaderInfo : shaderInfos)
        {
            if (shaderInfo.ShaderStage == shaderStage)
            {
                return context->CreateShaderModule(shaderInfo);
            }
        }


        //TODO: Always return sth;
    }

    void ShaderCompiler::WriteShaderToDisk(const ShaderCompilationDescription& shaderCompilationDescription, const ShaderInfo& shaderInfo) const
    {
        std::string baseName = shaderCompilationDescription.Name;

        // Check if name ends with .slang if so remove it for writing to disk
        const std::string extensionToRemove = ".slang";
        if (baseName.length() >= extensionToRemove.length() && baseName.rfind(extensionToRemove) == (baseName.length() - extensionToRemove.length()))
        {
            baseName.erase(baseName.length() - extensionToRemove.length());
        }

        // Construct the full output path
        //TODO: this will write it to a .cache folder in the shader folder, i don't know if this is what i want
        std::filesystem::path outputPath = ShaderFolder;
        EOS::ShaderStage shaderStage = shaderInfo.ShaderStage;
        outputPath.append(".cache").append(baseName + ShaderStageToString(shaderStage) + ".spirv"); //will become .cache/ShaderNameShaderStage.spirv


        // create a std::string that contains the raw binary data from outShaderInfo.spirv.
        const char* rawDataBytes = reinterpret_cast<const char*>(shaderInfo.Spirv.data());
        size_t rawDataSizeBytes = shaderInfo.Spirv.size() * sizeof(uint32_t);
        std::string contentToWrite(rawDataBytes, rawDataSizeBytes);


        //Write to disk
        EOS::WriteFile(outputPath, contentToWrite);
    }

    void ShaderCompiler::HandleEntryPoint(ShaderInfo& outShaderInfo, IModule* module, const char* shaderName, SlangInt32 entryPointIndex)
    {
        Slang::ComPtr<IEntryPoint> entryPoint;
        module->getDefinedEntryPoint(entryPointIndex, entryPoint.writeRef());
        CHECK(entryPoint, "Cannot find entrypoint for shader: '{}'.", shaderName);

        IComponentType* components[] = { module, entryPoint };
        Slang::ComPtr<IComponentType> program;
        Session->createCompositeComponentType(components, 2, program.writeRef());
        slang::ProgramLayout* layout = program->getLayout();

        // resolve all cross-module references
        // also used to resolve link time specializaions (https://shader-slang.org/slang/user-guide/link-time-specialization)
        Slang::ComPtr<IComponentType> linkedProgram;
        Slang::ComPtr<ISlangBlob> diagnosticBlob;
        program->link(linkedProgram.writeRef(), diagnosticBlob.writeRef());

        Slang::ComPtr<ISlangBlob> kernelBlob;
        linkedProgram->getEntryPointCode(0, 0, kernelBlob.writeRef(), Diagnostics.writeRef());
        if(Diagnostics)
        {
            EOS::Logger->critical("Slang Shader Compiler Diagnostics:\n{}", static_cast<const char *>(Diagnostics->getBufferPointer()));
            CHECK(false, "Shader Compile Error");
            Diagnostics.setNull();
        }

        Slang::ComPtr<IMetadata> metadata;
        linkedProgram->getEntryPointMetadata(0,0, metadata.writeRef(), Diagnostics.writeRef());
        if(Diagnostics)
        {
            EOS::Logger->critical("Slang Shader Compiler Diagnostics:\n{}", static_cast<const char *>(Diagnostics->getBufferPointer()));
            CHECK(false, "Shader Compile Error");
            Diagnostics.setNull();
        }

        bool isUsed;
        if (kernelBlob)
        {
            const void* bufferPtr = kernelBlob->getBufferPointer();
            size_t bufferSizeInBytes = kernelBlob->getBufferSize();

            CHECK(bufferSizeInBytes !=0, "Kernel blob is empty for shader '{}'.", shaderName);
            CHECK(bufferSizeInBytes % sizeof(uint32_t) == 0, "Kernel blob size ({}) for shader '{}' is not a multiple of sizeof(uint32_t). SPIR-V data may be corrupt or invalid.", bufferSizeInBytes, shaderName);

            const uint32_t* spirvWordData = static_cast<const uint32_t*>(bufferPtr);
            size_t spirvElementCount = bufferSizeInBytes / sizeof(uint32_t);

            // Populate outShaderInfo.spirv
            outShaderInfo.Spirv.clear();
            outShaderInfo.Spirv.assign(spirvWordData, spirvWordData + spirvElementCount);
        }


        // Start getting data out of the shader
        slang::ProgramLayout* reflection = linkedProgram->getLayout();
        slang::EntryPointReflection* entryPointLayout = reflection->getEntryPointByIndex(0);
        outShaderInfo.ShaderStage = ToShaderStage(entryPointLayout->getStage());

        // Get the size of the push constant.
        int totalPushConstantSize = 0;
        for (int i{}; i < reflection->getParameterCount(); ++i)
        {
            VariableLayoutReflection* variableLayout = reflection->getParameterByIndex(i);
            auto category = variableLayout->getCategory();

            // Whenever our pushconstant holds a struct of data
            //TODO: i need to know in what stage this pushConstant is actually used
            if ( category == slang::PushConstantBuffer)
            {
                metadata->isParameterLocationUsed(SLANG_PARAMETER_CATEGORY_VARYING_INPUT, variableLayout->getBindingSpace(), variableLayout->getBindingIndex(), isUsed);
                totalPushConstantSize += variableLayout->getTypeLayout()->getElementVarLayout()->getTypeLayout()->getSize();
                EOS::Logger->info("Found Shader Variable: {}, of Type: {}, as PushConstant in Shader: {}, with Stage: {}", variableLayout->getName(), variableLayout->getType()->getName(), shaderName, ShaderStageToString(outShaderInfo.ShaderStage));
            }
        }

        //TODO: this code will count the size of the pushconstants used in the file.
        EOS::Logger->info("PushConstantSize:{}", totalPushConstantSize);
        outShaderInfo.PushConstantSize = totalPushConstantSize;
        outShaderInfo.DebugName = shaderName;
    }
}
