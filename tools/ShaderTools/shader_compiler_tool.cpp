// EOSShaderCompilerTool: compiles every Slang module with entry points under the project and engine shader
// directories into the shader cache. Modules whose cache is up to date (same options, same Slang version, unchanged
// sources including imports) are skipped, so a build where no shader changed does not start Slang at all.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "ShaderTools/shaderCompiler.h"

namespace
{
    struct Options final
    {
        std::filesystem::path ProjectShaderDirectory;
        std::filesystem::path EngineShaderDirectory;
        std::filesystem::path OutputDirectory;
        std::vector<std::string> ModulesToReflect;
        std::filesystem::path SpirvDumpDirectory;
        bool Force = false;
    };

    void PrintUsage()
    {
        std::cerr << "Usage: EOSShaderCompilerTool --project-shaders <dir> --engine-shaders <dir> --output <dir> [--force] [--reflect <module>]...\n"
                     "  --force             recompile every module, even when its cache is up to date\n"
                     "  --reflect <module>  print the reflection of a module after compiling\n"
                     "  --dump-spirv <dir>  write the SPIR-V of every --reflect module to <dir>/<module>.<entry point>.spv\n";
    }

    [[nodiscard]] bool ParseOptions(int argc, char** argv, Options& outOptions)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            const bool hasValue = i + 1 < argc;

            if (argument == "--project-shaders" && hasValue) outOptions.ProjectShaderDirectory = argv[++i];
            else if (argument == "--engine-shaders" && hasValue) outOptions.EngineShaderDirectory = argv[++i];
            else if (argument == "--output" && hasValue) outOptions.OutputDirectory = argv[++i];
            else if (argument == "--force") outOptions.Force = true;
            else if (argument == "--reflect" && hasValue) outOptions.ModulesToReflect.emplace_back(argv[++i]);
            else if (argument == "--dump-spirv" && hasValue) outOptions.SpirvDumpDirectory = argv[++i];
            else return false;
        }

        return !outOptions.EngineShaderDirectory.empty() && !outOptions.OutputDirectory.empty();
    }

    // "eos/imgui.slang" relative to a search path is imported as "eos.imgui".
    [[nodiscard]] std::string ToModuleName(const std::filesystem::path& relativePath)
    {
        std::filesystem::path withoutExtension = relativePath;
        withoutExtension.replace_extension();

        std::string moduleName;
        for (const std::filesystem::path& part : withoutExtension)
        {
            if (!moduleName.empty()) moduleName += '.';
            moduleName += part.string();
        }

        return moduleName;
    }

    // Only modules that declare [shader("...")] entry points are programs; everything else is imported by them.
    // A false positive (the text inside a comment) is harmless: the module compiles to a program without entry points.
    [[nodiscard]] bool MayDeclareEntryPoints(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return content.find("[shader(") != std::string::npos;
    }

    [[nodiscard]] const char* ToString(EOS::ShaderScalarType type)
    {
        switch (type)
        {
            case EOS::ShaderScalarType::Bool:    return "bool";
            case EOS::ShaderScalarType::Int8:    return "int8";
            case EOS::ShaderScalarType::UInt8:   return "uint8";
            case EOS::ShaderScalarType::Int16:   return "int16";
            case EOS::ShaderScalarType::UInt16:  return "uint16";
            case EOS::ShaderScalarType::Int32:   return "int";
            case EOS::ShaderScalarType::UInt32:  return "uint";
            case EOS::ShaderScalarType::Int64:   return "int64";
            case EOS::ShaderScalarType::UInt64:  return "uint64";
            case EOS::ShaderScalarType::Float16: return "half";
            case EOS::ShaderScalarType::Float32: return "float";
            case EOS::ShaderScalarType::Float64: return "double";
            case EOS::ShaderScalarType::Unknown: return "?";
        }

        return "?";
    }

    [[nodiscard]] const char* ToString(EOS::ShaderResourceType type)
    {
        switch (type)
        {
            case EOS::ShaderResourceType::Sampler:                return "sampler";
            case EOS::ShaderResourceType::SampledTexture:         return "sampled texture";
            case EOS::ShaderResourceType::StorageTexture:         return "storage texture";
            case EOS::ShaderResourceType::CombinedTextureSampler: return "combined texture sampler";
            case EOS::ShaderResourceType::UniformBuffer:          return "uniform buffer";
            case EOS::ShaderResourceType::StorageBuffer:          return "storage buffer";
            case EOS::ShaderResourceType::AccelerationStructure:  return "acceleration structure";
            case EOS::ShaderResourceType::Bindless:               return "bindless (aliased)";
            case EOS::ShaderResourceType::Unknown:                return "unknown";
        }

        return "unknown";
    }

    void PrintVaryings(const char* label, const std::vector<EOS::ShaderVarying>& varyings)
    {
        for (const EOS::ShaderVarying& varying : varyings)
        {
            std::printf("    %s location %u: %s%u %s : %s%u\n", label, varying.Location, ToString(varying.ComponentType), varying.ComponentCount,
                        varying.Name.c_str(), varying.Semantic.c_str(), varying.SemanticIndex);
        }
    }

    void PrintReflection(const EOS::CompiledShaderProgram& program)
    {
        std::printf("module %s (Slang %s), push constants: %u bytes\n", program.Description.Module.c_str(), program.CompilerVersion.c_str(), program.PushConstantSize);

        for (const EOS::ShaderEntryPoint& entryPoint : program.EntryPoints)
        {
            std::printf("  entry point %s [%s], push constants %u bytes, %zu SPIR-V words", entryPoint.Name.c_str(), EOS::ToString(entryPoint.Stage), entryPoint.PushConstantSize, entryPoint.Spirv.size());
            if (entryPoint.ThreadGroupSize[0] != 0) std::printf(", thread group %ux%ux%u", entryPoint.ThreadGroupSize[0], entryPoint.ThreadGroupSize[1], entryPoint.ThreadGroupSize[2]);
            std::printf("\n");
            PrintVaryings("in ", entryPoint.Inputs);
            PrintVaryings("out", entryPoint.Outputs);
        }

        for (const EOS::ShaderSpecializationConstant& constant : program.SpecializationConstants)
        {
            std::printf("  specialization constant %s: %s, constant_id %u\n", constant.Name.c_str(), ToString(constant.Type), constant.ConstantID);
        }

        for (const EOS::ShaderResourceBinding& binding : program.ResourceBindings)
        {
            std::printf("  binding %s: set %u binding %u, %s, used by stages:", binding.Name.c_str(), binding.Set, binding.Binding, ToString(binding.Type));
            for (uint32_t stage = 0; stage < static_cast<uint32_t>(EOS::ShaderStage::None); ++stage)
            {
                if (binding.StageMask & (1u << stage)) std::printf(" %s", EOS::ToString(static_cast<EOS::ShaderStage>(stage)));
            }
            std::printf("\n");
        }

        for (const EOS::ShaderSourceDependency& dependency : program.Dependencies)
        {
            std::printf("  source %s\n", dependency.Path.string().c_str());
        }
    }

    // Module name -> file. Directories are scanned in search-path order, so when two directories define the same
    // module the first one wins, exactly as it does when Slang resolves an import.
    void CollectModules(const std::filesystem::path& directory, std::map<std::string, std::filesystem::path>& inOutModules)
    {
        std::error_code errorCode;
        if (directory.empty() || !std::filesystem::is_directory(directory, errorCode)) return;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, errorCode))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".slang") continue;

            const std::string moduleName = ToModuleName(std::filesystem::relative(entry.path(), directory));
            const auto [it, inserted] = inOutModules.try_emplace(moduleName, entry.path());
            if (!inserted)
            {
                std::cout << "[shader-tool] warning: " << entry.path().string() << " is hidden by " << it->second.string()
                          << "; both define module '" << moduleName << "'\n";
            }
        }
    }
}

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage();
        return 1;
    }

    // Same search order as VulkanContext: project shaders, then engine shaders.
    std::vector<std::filesystem::path> searchPaths;
    if (!options.ProjectShaderDirectory.empty()) searchPaths.push_back(options.ProjectShaderDirectory);
    searchPaths.push_back(options.EngineShaderDirectory);

    std::map<std::string, std::filesystem::path> modules;
    for (const std::filesystem::path& searchPath : searchPaths) CollectModules(searchPath, modules);

    EOS::ShaderCompiler compiler(options.OutputDirectory, searchPaths);

    const auto startTime = std::chrono::steady_clock::now();
    uint32_t numberUpToDate = 0;
    uint32_t numberCompiled = 0;
    uint32_t numberFailed = 0;

    for (const auto& [moduleName, path] : modules)
    {
        if (!MayDeclareEntryPoints(path)) continue;

        const EOS::ShaderProgramDescription description{.Module = moduleName};
        if (!options.Force && compiler.IsCacheUpToDate(description))
        {
            ++numberUpToDate;
            continue;
        }

        std::string diagnostics;
        const std::shared_ptr<const EOS::CompiledShaderProgram> program = compiler.CompileProgram(description, diagnostics);
        if (!program)
        {
            ++numberFailed;
            std::cerr << "[shader-tool] error: " << moduleName << " (" << path.string() << ")\n" << diagnostics;
            continue;
        }

        ++numberCompiled;
        std::cout << "[shader-tool] compiled " << moduleName << " (" << program->EntryPoints.size() << " entry points)\n";
        if (!diagnostics.empty()) std::cout << diagnostics;
    }

    for (const std::string& moduleName : options.ModulesToReflect)
    {
        std::string diagnostics;
        const std::shared_ptr<const EOS::CompiledShaderProgram> program = compiler.LoadProgram({.Module = moduleName}, diagnostics);
        if (!program)
        {
            ++numberFailed;
            std::cerr << "[shader-tool] error: " << moduleName << "\n" << diagnostics;
            continue;
        }

        PrintReflection(*program);

        if (!options.SpirvDumpDirectory.empty())
        {
            std::filesystem::create_directories(options.SpirvDumpDirectory);
            for (const EOS::ShaderEntryPoint& entryPoint : program->EntryPoints)
            {
                const std::filesystem::path path = options.SpirvDumpDirectory / (moduleName + "." + entryPoint.Name + ".spv");
                std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(entryPoint.Spirv.data()), static_cast<std::streamsize>(entryPoint.Spirv.size() * sizeof(uint32_t)));
                std::cout << "[shader-tool] wrote " << path.string() << "\n";
            }
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);
    std::cout << "[shader-tool] " << numberCompiled << " compiled, " << numberUpToDate << " up to date, " << numberFailed << " failed ("
              << elapsed.count() << " ms) -> " << compiler.GetCacheDirectory().string() << "\n";

    return numberFailed == 0 ? 0 : 1;
}
