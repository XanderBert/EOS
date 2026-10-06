// EOSShaderCompilerTool: compiles every Slang module with entry points under the project and engine shader
// directories into the shader cache, and writes the C++ mirrors of the project's [CppExport] structs. Modules whose
// cache or header is up to date (same options, same Slang version, unchanged sources including imports) are skipped,
// so a build where no shader changed does not start Slang at all.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ShaderTools/shaderCache.h"
#include "ShaderTools/shaderCompiler.h"

namespace
{
    struct Options final
    {
        std::filesystem::path ProjectShaderDirectory;
        std::filesystem::path EngineShaderDirectory;
        std::filesystem::path OutputDirectory;
        std::filesystem::path CppOutputDirectory;
        std::vector<std::filesystem::path> LibraryCacheRoots;
        std::vector<std::string> ModulesToReflect;
        std::filesystem::path SpirvDumpDirectory;
        bool Force = false;
        bool LibraryOnly = false;
    };

    // C++ namespace of the engine's mirrored types; a project's go in the global namespace.
    constexpr const char* kEngineNamespace = "EOS";

    void PrintUsage()
    {
        std::cerr << "Usage: EOSShaderCompilerTool [--project-shaders <dir>] --engine-shaders <dir> --output <dir> [options]\n"
                     "  --cpp-output <dir>  write C++ mirrors of the [CppExport] structs of the project's modules (or the engine's,\n"
                     "                      without --project-shaders) to <dir>/<module path>.h\n"
                     "  --library-only      compile no programs; precompile every module (Slang IR) and write the C++ mirrors\n"
                     "  --library-cache <dir>  load the modules a --library-only run precompiled into <dir>\n"
                     "  --force             recompile every module and regenerate every header, even when up to date\n"
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
            else if (argument == "--cpp-output" && hasValue) outOptions.CppOutputDirectory = argv[++i];
            else if (argument == "--library-only") outOptions.LibraryOnly = true;
            else if (argument == "--library-cache" && hasValue) outOptions.LibraryCacheRoots.emplace_back(argv[++i]);
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

    [[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // Only modules that declare [shader("...")] entry points are programs; everything else is imported by them.
    // A false positive (the text inside a comment) is harmless: the module compiles to a program without entry points.
    [[nodiscard]] bool MayDeclareEntryPoints(const std::filesystem::path& path)
    {
        return ReadFile(path).find("[shader(") != std::string::npos;
    }

    // Same idea for [CppExport]: a false positive generates nothing.
    [[nodiscard]] bool MayExportToCpp(const std::filesystem::path& path)
    {
        return ReadFile(path).find("CppExport") != std::string::npos;
    }

    [[nodiscard]] bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& directory)
    {
        const std::filesystem::path relative = std::filesystem::absolute(path).lexically_normal().lexically_relative(std::filesystem::absolute(directory).lexically_normal());
        return !relative.empty() && *relative.begin() != "..";
    }

    // "eos.material" -> "eos/material"
    [[nodiscard]] std::filesystem::path ToModulePath(const std::string& moduleName)
    {
        std::string path = moduleName;
        std::ranges::replace(path, '.', '/');
        return path;
    }

    constexpr const char* kGeneratedHeaderBanner = "// Generated by EOSShaderCompilerTool";

    // Generated headers are compared and hashed without carriage returns, so a checkout with Windows line endings is
    // not regenerated on every build.
    [[nodiscard]] uint64_t HashHeaderText(std::string text)
    {
        std::erase(text, '\r');
        return EOS::ShaderCache::HashString(text, 0);
    }

    // What a generated header was made from (a ShaderCache::DerivedEntry whose payload is the header's hash, empty
    // when the module exports nothing). Lets a build where no shader changed skip Slang, as the program cache does.
    [[nodiscard]] uint64_t GetHeaderStampKey()
    {
        return EOS::ShaderCache::HashString(EOS::ShaderCompiler::GetCompilerVersion(), EOS::CppGeneratorVersion);
    }

    [[nodiscard]] std::string ToStampPayload(const std::string& headerText)
    {
        if (headerText.empty()) return {};

        const uint64_t hash = HashHeaderText(headerText);
        return {reinterpret_cast<const char*>(&hash), sizeof(hash)};
    }

    [[nodiscard]] bool IsHeaderUpToDate(const std::filesystem::path& stampPath, const std::filesystem::path& headerPath, bool& outHasHeader)
    {
        EOS::ShaderCache::DerivedEntry stamp;
        if (!EOS::ShaderCache::ReadDerived(stampPath, GetHeaderStampKey(), stamp) || !EOS::ShaderCache::AreDependenciesUpToDate(stamp.Dependencies)) return false;

        std::error_code errorCode;
        outHasHeader = !stamp.Payload.empty();
        if (!outHasHeader) return !std::filesystem::exists(headerPath, errorCode);
        return std::filesystem::exists(headerPath, errorCode) && ToStampPayload(ReadFile(headerPath)) == stamp.Payload;
    }

    // Leaves the file (and its timestamp) alone when the text is unchanged, so nothing that includes it rebuilds.
    [[nodiscard]] bool WriteIfChanged(const std::filesystem::path& path, const std::string& text, bool& outChanged)
    {
        outChanged = false;
        std::error_code errorCode;
        if (std::filesystem::exists(path, errorCode) && HashHeaderText(ReadFile(path)) == HashHeaderText(text)) return true;

        std::filesystem::create_directories(path.parent_path(), errorCode);
        const std::filesystem::path temporaryPath = path.string() + ".tmp";
        {
            std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!file) return false;
        }

        std::filesystem::rename(temporaryPath, path, errorCode);
        outChanged = !errorCode;
        return !errorCode;
    }

    [[nodiscard]] bool IsGeneratedHeader(const std::filesystem::path& path)
    {
        return ReadFile(path).starts_with(kGeneratedHeaderBanner);
    }

    struct CppHeaderCounts final
    {
        uint32_t Written = 0;
        uint32_t UpToDate = 0;
        uint32_t Failed = 0;
    };

    // Writes <outputDirectory>/<module path>.h for every module under ownRoot that has [CppExport] structs, and removes
    // generated headers whose module no longer exports anything.
    void GenerateCppHeaders(EOS::ShaderCompiler& compiler, const std::map<std::string, std::filesystem::path>& modules, const std::filesystem::path& ownRoot,
                            const std::vector<EOS::CppExportRoot>& roots, const std::filesystem::path& outputDirectory, bool force, CppHeaderCounts& inOutCounts)
    {
        std::set<std::filesystem::path> expectedHeaders;

        for (const auto& [moduleName, path] : modules)
        {
            if (!IsUnder(path, ownRoot) || !MayExportToCpp(path)) continue;

            const std::filesystem::path headerPath = (outputDirectory / ToModulePath(moduleName)).replace_extension(".h");
            const std::filesystem::path stampPath = compiler.GetCacheDirectory() / "cpp" / (moduleName + EOS::ShaderCache::FileExtension);

            bool hasHeader = false;
            if (!force && IsHeaderUpToDate(stampPath, headerPath, hasHeader))
            {
                if (hasHeader) expectedHeaders.insert(headerPath);
                ++inOutCounts.UpToDate;
                continue;
            }

            EOS::GeneratedCppHeader header;
            std::string diagnostics;
            if (!compiler.GenerateCppHeader(moduleName, roots, header, diagnostics))
            {
                ++inOutCounts.Failed;
                std::cerr << "[shader-tool] error: C++ header for " << moduleName << " (" << path.string() << ")\n" << diagnostics;

                // Keep the previous header, so its includes still compile while the shader is being fixed.
                std::error_code errorCode;
                if (std::filesystem::exists(headerPath, errorCode)) expectedHeaders.insert(headerPath);
                continue;
            }

            if (!diagnostics.empty()) std::cout << diagnostics;

            if (!header.Text.empty())
            {
                bool changed = false;
                if (!WriteIfChanged(headerPath, header.Text, changed))
                {
                    ++inOutCounts.Failed;
                    std::cerr << "[shader-tool] error: could not write " << headerPath.string() << "\n";
                    continue;
                }

                expectedHeaders.insert(headerPath);
                if (changed)
                {
                    ++inOutCounts.Written;
                    std::cout << "[shader-tool] generated " << headerPath.string() << "\n";
                }
            }

            std::string stampError;
            if (!EOS::ShaderCache::WriteDerived(stampPath, {.Key = GetHeaderStampKey(), .Source = path, .Dependencies = std::move(header.Dependencies), .Payload = ToStampPayload(header.Text)}, stampError))
            {
                std::cout << "[shader-tool] warning: " << stampError << "; the header for " << moduleName << " is regenerated next build\n";
            }
        }

        std::error_code errorCode;
        if (!std::filesystem::is_directory(outputDirectory, errorCode)) return;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(outputDirectory, errorCode))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".h" || expectedHeaders.contains(entry.path()) || !IsGeneratedHeader(entry.path())) continue;

            std::filesystem::remove(entry.path(), errorCode);
            std::cout << "[shader-tool] removed " << entry.path().string() << " (its module no longer has [CppExport] structs)\n";
        }
    }

    [[nodiscard]] const char* ToString(EOS::ShaderScalarType type)
    {
        switch (type)
        {
            case EOS::ShaderScalarType::Boolean:    return "bool";
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

    EOS::ShaderCompilerOptions compilerOptions = EOS::ShaderCompilerOptions::Default();
    compilerOptions.LibraryCacheRoots = options.LibraryCacheRoots;
    EOS::ShaderCompiler compiler(options.OutputDirectory, searchPaths, std::move(compilerOptions));

    const auto startTime = std::chrono::steady_clock::now();
    uint32_t numberUpToDate = 0;
    uint32_t numberCompiled = 0;
    uint32_t numberFailed = 0;

    for (const auto& [moduleName, path] : modules)
    {
        if (options.LibraryOnly || !MayDeclareEntryPoints(path)) continue;

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

    // A library run precompiles every module of its own directory, so programs that import them skip parsing and
    // checking them. Done before the C++ headers, which then load the precompiled modules too.
    uint32_t numberPrecompiled = 0;
    if (options.LibraryOnly)
    {
        for (const auto& [moduleName, path] : modules)
        {
            if (!IsUnder(path, searchPaths.front())) continue;
            if (!options.Force && compiler.IsPrecompiledModuleUpToDate(moduleName))
            {
                ++numberUpToDate;
                continue;
            }

            std::string diagnostics;
            if (!compiler.PrecompileModule(moduleName, diagnostics))
            {
                ++numberFailed;
                std::cerr << "[shader-tool] error: " << moduleName << " (" << path.string() << ")\n" << diagnostics;
                continue;
            }

            ++numberPrecompiled;
            std::cout << "[shader-tool] precompiled " << moduleName << "\n";
            if (!diagnostics.empty()) std::cout << diagnostics;
        }
    }

    CppHeaderCounts headerCounts;
    if (!options.CppOutputDirectory.empty())
    {
        // The project's own types go in the global namespace, the engine's in EOS. Without a project directory this
        // run builds the engine itself.
        const bool isProject = !options.ProjectShaderDirectory.empty();
        const std::filesystem::path ownRoot = isProject ? options.ProjectShaderDirectory : options.EngineShaderDirectory;

        std::vector<EOS::CppExportRoot> roots{{.ShaderDirectory = ownRoot, .Namespace = isProject ? "" : kEngineNamespace}};
        if (isProject) roots.push_back({.ShaderDirectory = options.EngineShaderDirectory, .Namespace = kEngineNamespace});

        GenerateCppHeaders(compiler, modules, ownRoot, roots, options.CppOutputDirectory, options.Force, headerCounts);
        numberFailed += headerCounts.Failed;
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
    std::cout << "[shader-tool] " << numberCompiled << " compiled, ";
    if (options.LibraryOnly) std::cout << numberPrecompiled << " modules precompiled, ";
    std::cout << numberUpToDate << " up to date, ";
    if (!options.CppOutputDirectory.empty()) std::cout << headerCounts.Written << " C++ headers written, " << headerCounts.UpToDate << " up to date, ";
    std::cout << numberFailed << " failed (" << elapsed.count() << " ms) -> " << compiler.GetCacheDirectory().string() << "\n";

    return numberFailed == 0 ? 0 : 1;
}
