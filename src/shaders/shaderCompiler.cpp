#include "shaderCompiler.h"
#include <fstream>

#include "logger.h"
#include "utils.h"
#include "spdlog/fmt/bundled/os.h"

namespace EOS
{
#if defined(EOS_SHADER_COMPILER)
    using namespace slang;
#endif

    //https://shader-slang.org/slang/user-guide/compiling.html#using-the-compilation-api
    ShaderCompiler::ShaderCompiler(const std::filesystem::path& shaderFolder)
    : ShaderFolder(shaderFolder)
    {
#if defined(EOS_SHADER_COMPILER)
        //Create a Global Session, This is not threadsafe, so if we want to multithread shader compilation we need to create a global session for each thread
        SlangGlobalSessionDesc sessionDescription = {};
        SLANG_ASSERT_VOID_ON_FAIL(createGlobalSession(&sessionDescription, GlobalSession.writeRef()));
#endif
    }
#if defined(EOS_SHADER_COMPILER)
    bool ShaderCompiler::CompileShader(const ShaderCompilationDescription& shaderCompilationDescription, std::vector<ShaderInfo>& outShaderInfo)
    {
        CompilerOptionEntry compilerOptions[] =
        {
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "SPV_GOOGLE_user_type"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvDerivativeControl"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvImageQuery"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvImageGatherExtended"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvSparseResidency"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvMinLod"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvFragmentFullyCoveredEXT"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvRayTracingPositionFetchKHR"}},
            {.name = CompilerOptionName::Capability,
             .value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "spvRayQueryKHR"}},
            {.name = CompilerOptionName::Optimization,
            .value = {.kind = CompilerOptionValueKind::Int, .intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL}},
            {.name = CompilerOptionName::EmitSpirvDirectly,
            .value = {.kind = CompilerOptionValueKind::Int, .intValue0 = 1}},

#ifdef EOS_DEBUG //TODO: make option on context description
            {.name = CompilerOptionName::DebugInformation,
                    .value = {.kind = CompilerOptionValueKind::Int, .intValue0 = 2}},
#endif
        };

        const TargetDesc targetDesc
        {
            .format = SLANG_SPIRV,
            .profile = GlobalSession->findProfile("spirv_1_6"),
            .flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
            .forceGLSLScalarBufferLayout = true,
            .compilerOptionEntries = &compilerOptions[0],
            .compilerOptionEntryCount = ARRAY_COUNT(compilerOptions),
        };

        const SessionDesc sessionDesc
        {
            .targets = &targetDesc,
            .targetCount = 1,
        };
        GlobalSession->createSession(sessionDesc, Session.writeRef());

        // Will load the .slang file
        IModule* module = Session->loadModule(shaderCompilationDescription.Name, Diagnostics.writeRef());
        if (Diagnostics)
        {
            std::string msgStr(static_cast<const char*>(Diagnostics->getBufferPointer()));
            std::string_view msgView(msgStr);

            bool isError = msgView.find("error[") != std::string_view::npos;
            bool isWarning = msgView.find("warning[") != std::string_view::npos;

            if (isError)
            {
                EOS::Logger->error("Slang Shader Compiler Error:\n\n\033[31m{}\033[0m", msgStr);
                return false;
            }

            if (isWarning)
            {
                EOS::Logger->warn("\033[33m{}\033[0m", msgStr);
                Diagnostics.setNull();
            }
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
            if (shaderCompilationDescription.Cache)
            {
                CacheShader(shaderCompilationDescription, outShaderInfo[i]);
            }
        }

        return true;
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
#endif
    bool ShaderCompiler::LoadShader(const char* fileName, EOS::ShaderStage shaderStage, ShaderInfo& outShaderInfo,  bool invalidate)
    {

#if defined(EOS_SHADER_COMPILER)
        //We never want to recompile shaders when there is no compiler
        invalidate = false;
#endif

        if (!invalidate)
        {
            // First we check if the corresponding shaderfile and all of its entry points have been compiled yet,
            const std::filesystem::path cachedFilePath = fmt::format(".cache/{}{}{}", fileName, ShaderStageToString(shaderStage), ShaderFileFormat);
            std::ifstream file(cachedFilePath, std::ios::in | std::ios::binary);
            if (file.is_open())
            {
                file.close();
                EOS::Logger->debug("{} shader was already compiled and cached. Loading from cache.", fileName);

                LoadShaderFromCache(cachedFilePath, outShaderInfo);
                return true;
            }
        }

#if defined(EOS_SHADER_COMPILER)
        //Compile all shaders in the file and cache if needed
        EOS::Logger->debug("{} Has not been Compiled yet.", fileName);
        std::vector<ShaderInfo> shaderInfos{};
        bool success = CompileShader({fileName}, shaderInfos);
        if (!success) return false;

        for (const auto& shaderInfo : shaderInfos)
        {
            if(shaderInfo.ShaderStage == shaderStage)
            {
                outShaderInfo = shaderInfo;
                return true;
            }
        }

        CHECK(false, "Could not Load: {} - {}", fileName, ShaderStageToString(shaderStage));
#endif
        return false;
    }


#if defined(EOS_SHADER_COMPILER)
    void ShaderCompiler::CacheShader(const ShaderCompilationDescription& shaderCompilationDescription, const ShaderInfo& shaderInfo) const
    {
        std::string baseName = shaderCompilationDescription.Name;

        // Check if name ends with .slang if so remove it for writing to disk
        const std::string extensionToRemove = ".slang";
        if (baseName.length() >= extensionToRemove.length() && baseName.rfind(extensionToRemove) == (baseName.length() - extensionToRemove.length()))
        {
            baseName.erase(baseName.length() - extensionToRemove.length());
        }

        // Construct the full output path
        std::filesystem::path outputPath = ShaderFolder;
        EOS::ShaderStage shaderStage = shaderInfo.ShaderStage;
        outputPath.append(".cache").append(baseName + ShaderStageToString(shaderStage) + ShaderFileFormat); //will become .cache/ShaderNameShaderStage.ShaderFileFormat


        //Write Binary
        std::ofstream file(outputPath, std::ios::binary);

        CachedShaderHeader header;
        header.stage = shaderStage;
        header.pushConstantSize = shaderInfo.PushConstantSize;
        header.debugNameLength = shaderInfo.DebugName.length();
        header.spirvSize = shaderInfo.Spirv.size() * sizeof(uint32_t);

        //TODO: Create a debug option to also emmit raw SPIR-V
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(shaderInfo.DebugName.c_str(), header.debugNameLength);
        file.write(reinterpret_cast<const char*>(shaderInfo.Spirv.data()), header.spirvSize);
    }


    void ShaderCompiler::HandleEntryPoint(ShaderInfo& outShaderInfo, IModule* module, const char* shaderName, SlangInt32 entryPointIndex)
    {
        Slang::ComPtr<IEntryPoint> entryPoint;
        module->getDefinedEntryPoint(entryPointIndex, entryPoint.writeRef());
        CHECK(entryPoint, "Cannot find entrypoint for shader: '{}'.", shaderName);

        IComponentType* components[] = { module, entryPoint };
        Slang::ComPtr<IComponentType> program;
        Session->createCompositeComponentType(components, 2, program.writeRef());

        // resolve all cross-module references
        // also used to resolve link time specializations (https://shader-slang.org/slang/user-guide/link-time-specialization)
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


        // Start Reflection
        Slang::ComPtr<IMetadata> metadata;
        linkedProgram->getEntryPointMetadata(0,0, metadata.writeRef(), Diagnostics.writeRef());
        if(Diagnostics)
        {
            EOS::Logger->critical("Slang Shader Compiler Diagnostics:\n{}", static_cast<const char *>(Diagnostics->getBufferPointer()));
            CHECK(false, "Shader Compile Error");
            Diagnostics.setNull();
        }

        ProgramLayout* reflection = linkedProgram->getLayout();
        EntryPointReflection* entryPointLayout = reflection->getEntryPointByIndex(0);
        outShaderInfo.ShaderStage = ToShaderStage(entryPointLayout->getStage());

        //TODO: this code will count the size of the pushconstants used in the file
        uint32_t totalPushConstantSize = 0;
        for (int i{}; i < reflection->getParameterCount(); ++i)
        {
            VariableLayoutReflection* variableLayout = reflection->getParameterByIndex(i);
            if ( variableLayout->getCategory() == slang::PushConstantBuffer)
            {
                totalPushConstantSize += variableLayout->getTypeLayout()->getElementVarLayout()->getTypeLayout()->getSize();
                EOS::Logger->debug("Found Shader Variable: {}, of Type: {}, as PushConstant in Shader: {}, with Stage: {}", variableLayout->getName(), variableLayout->getType()->getName(), shaderName, ShaderStageToString(outShaderInfo.ShaderStage));
            }
        }

        outShaderInfo.PushConstantSize = totalPushConstantSize;
        outShaderInfo.DebugName = shaderName;
    }
#endif

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

    void ShaderCompiler::LoadShaderFromCache(const std::filesystem::path& path, ShaderInfo& outInfo)
    {
        std::ifstream file(path, std::ios::binary);
        CHECK(file, "Could not find cached shader file: {}", path.string());

        CachedShaderHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        CHECK(header.checksum == EOS_SHADER_CHECKSUM,"Loaded shader cache: {}, got corrupted!", path.string());

        outInfo.ShaderStage = header.stage;
        outInfo.PushConstantSize = header.pushConstantSize;

        outInfo.DebugName.resize(header.debugNameLength);
        file.read(outInfo.DebugName.data(), header.debugNameLength);

        outInfo.Spirv.resize(header.spirvSize / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(outInfo.Spirv.data()), header.spirvSize);
    }
}
