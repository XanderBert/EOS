#include "shaderCompiler.h"

#include <algorithm>
#include <set>

#include "shaderCache.h"

#if defined(EOS_SHADER_TOOLS)
#include "shaderReflection.h"
#endif

namespace EOS
{
    namespace
    {
#if defined(EOS_DEBUG)
        constexpr const char* kConfigurationName = "Debug";
#else
        constexpr const char* kConfigurationName = "Release";
#endif

        constexpr const char* kTargetProfile = "spirv_1_6";

        // Capabilities shaders may use on top of the base profile. Declaring them only allows their use (and silences
        // Slang's "profile implicitly upgraded" warning); Slang still only emits the ones a shader actually needs, so
        // the device has to support whatever a given shader uses.
        constexpr const char* kTargetCapabilities[] =
        {
            "SPV_GOOGLE_user_type",
            "spvDerivativeControl",
            "spvImageQuery",
            "spvImageGatherExtended",
            "spvSparseResidency",
            "spvMinLod",
            "spvFragmentFullyCoveredEXT",
            "spvRayTracingPositionFetchKHR",
            "spvRayQueryKHR",
            "spvGroupNonUniformVote",
            "spvGroupNonUniformBallot",
            "spvGroupNonUniformArithmetic",
            "spvGroupNonUniformShuffle",
            "spvGroupNonUniformQuad",
        };

        // Everything below changes the generated code, so it is all part of the cache key.
        // Matrices are row-major and shaders multiply row vectors (mul(v, M)), matching glm's column-major memory layout.
        [[nodiscard]] uint64_t HashOptions(const ShaderCompilerOptions& options)
        {
            uint64_t hash = ShaderCache::HashString("target=spirv;matrix=row_major;buffer_layout=scalar;entry_point_names=1", 0);
            hash = ShaderCache::HashString(kTargetProfile, hash);
            for (const char* capability : kTargetCapabilities) hash = ShaderCache::HashString(capability, hash);

            hash = ShaderCache::HashString(options.DebugInfo ? "debug_info=1" : "debug_info=0", hash);
            hash = ShaderCache::HashString(std::to_string(static_cast<int>(options.Optimization)), hash);
            for (const ShaderMacro& define : options.GlobalDefines)
            {
                hash = ShaderCache::HashString(define.Name, hash);
                hash = ShaderCache::HashString(define.Value, hash);
            }

            return hash;
        }

        [[nodiscard]] std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string utf8 = path.u8string();
            return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
        }

        [[nodiscard]] std::filesystem::path FromUtf8(const char* text)
        {
            const std::string_view view(text);
            return std::filesystem::path(std::u8string(view.begin(), view.end()));
        }
    }

    ShaderCompilerOptions ShaderCompilerOptions::Default()
    {
#if defined(EOS_DEBUG)
        return {.DebugInfo = true, .Optimization = ShaderOptimizationLevel::None};
#else
        return {.DebugInfo = false, .Optimization = ShaderOptimizationLevel::High};
#endif
    }

#if defined(EOS_SHADER_TOOLS)
    namespace
    {
        void AppendDiagnostics(std::string& outDiagnostics, ISlangBlob* blob)
        {
            if (!blob || blob->getBufferSize() == 0) return;

            const char* text = static_cast<const char*>(blob->getBufferPointer());
            size_t size = blob->getBufferSize();
            while (size > 0 && text[size - 1] == '\0') --size;

            outDiagnostics.append(text, size);
            if (size > 0 && text[size - 1] != '\n') outDiagnostics += '\n';
        }

        [[nodiscard]] SlangOptimizationLevel ToSlangOptimizationLevel(ShaderOptimizationLevel level)
        {
            switch (level)
            {
                case ShaderOptimizationLevel::None:    return SLANG_OPTIMIZATION_LEVEL_NONE;
                case ShaderOptimizationLevel::Default: return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
                case ShaderOptimizationLevel::High:    return SLANG_OPTIMIZATION_LEVEL_HIGH;
                case ShaderOptimizationLevel::Maximal: return SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
            }

            return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
        }

        [[nodiscard]] std::string ListDefinedEntryPoints(slang::IModule* module)
        {
            std::string names;
            for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); ++i)
            {
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                module->getDefinedEntryPoint(i, entryPoint.writeRef());
                if (!entryPoint) continue;

                if (!names.empty()) names += ", ";
                names += entryPoint->getFunctionReflection()->getName();
            }

            return names.empty() ? std::string("none") : names;
        }
    }

    struct ShaderCompiler::SlangState final
    {
        Slang::ComPtr<slang::IGlobalSession> GlobalSession;

        // One session per distinct set of defines. A session caches every module it loads, so programs compiled
        // in the same session share the work of parsing and checking common imports. ResetModuleCache() clears this.
        std::unordered_map<uint64_t, Slang::ComPtr<slang::ISession>> Sessions;

        [[nodiscard]] slang::ISession* GetSession(const ShaderCompilerOptions& options, const std::vector<std::filesystem::path>& searchPaths, const std::vector<ShaderMacro>& defines, std::string& outDiagnostics)
        {
            uint64_t definesHash = 0;
            for (const ShaderMacro& define : defines)
            {
                definesHash = ShaderCache::HashString(define.Name, definesHash);
                definesHash = ShaderCache::HashString(define.Value, definesHash);
            }

            if (const auto it = Sessions.find(definesHash); it != Sessions.end()) return it->second;

            if (!GlobalSession)
            {
                // Creating the global session loads Slang's core module, which takes a noticeable moment, so it is
                // only done once something actually needs compiling.
                const SlangGlobalSessionDesc globalSessionDescription{};
                if (SLANG_FAILED(slang::createGlobalSession(&globalSessionDescription, GlobalSession.writeRef())))
                {
                    outDiagnostics += "error: could not create the Slang global session.\n";
                    return nullptr;
                }
            }

            std::vector<slang::CompilerOptionEntry> targetOptions;
            for (const char* capability : kTargetCapabilities)
            {
                targetOptions.push_back({slang::CompilerOptionName::Capability, {.kind = slang::CompilerOptionValueKind::String, .stringValue0 = capability}});
            }

            // Keep the entry point names in the SPIR-V instead of renaming every entry point to "main".
            targetOptions.push_back({slang::CompilerOptionName::VulkanUseEntryPointName, {.kind = slang::CompilerOptionValueKind::Int, .intValue0 = 1}});
            targetOptions.push_back({slang::CompilerOptionName::Optimization, {.kind = slang::CompilerOptionValueKind::Int, .intValue0 = static_cast<int32_t>(ToSlangOptimizationLevel(options.Optimization))}});
            if (options.DebugInfo)
            {
                targetOptions.push_back({slang::CompilerOptionName::DebugInformation, {.kind = slang::CompilerOptionValueKind::Int, .intValue0 = static_cast<int32_t>(SLANG_DEBUG_INFO_LEVEL_STANDARD)}});
            }

            const slang::TargetDesc targetDescription
            {
                .format = SLANG_SPIRV,
                .profile = GlobalSession->findProfile(kTargetProfile),
                .forceGLSLScalarBufferLayout = true,
                .compilerOptionEntries = targetOptions.data(),
                .compilerOptionEntryCount = static_cast<uint32_t>(targetOptions.size()),
            };

            std::vector<std::string> searchPathStrings;
            std::vector<const char*> searchPathPointers;
            searchPathStrings.reserve(searchPaths.size());
            for (const std::filesystem::path& searchPath : searchPaths)
            {
                searchPathStrings.push_back(ToUtf8(searchPath));
                searchPathPointers.push_back(searchPathStrings.back().c_str());
            }

            std::vector<slang::PreprocessorMacroDesc> macros;
            macros.reserve(options.GlobalDefines.size() + defines.size());
            for (const ShaderMacro& define : options.GlobalDefines) macros.push_back({define.Name.c_str(), define.Value.c_str()});
            for (const ShaderMacro& define : defines) macros.push_back({define.Name.c_str(), define.Value.c_str()});

            const slang::SessionDesc sessionDescription
            {
                .targets = &targetDescription,
                .targetCount = 1,
                .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR,
                .searchPaths = searchPathPointers.data(),
                .searchPathCount = static_cast<SlangInt>(searchPathPointers.size()),
                .preprocessorMacros = macros.data(),
                .preprocessorMacroCount = static_cast<SlangInt>(macros.size()),
            };

            Slang::ComPtr<slang::ISession> session;
            if (SLANG_FAILED(GlobalSession->createSession(sessionDescription, session.writeRef())))
            {
                outDiagnostics += "error: could not create a Slang session.\n";
                return nullptr;
            }

            return Sessions.emplace(definesHash, session).first->second;
        }
    };
#else
    struct ShaderCompiler::SlangState final {};
#endif

    ShaderCompiler::ShaderCompiler(const std::filesystem::path& cacheRoot, std::vector<std::filesystem::path> searchPaths, ShaderCompilerOptions options)
    : CacheDirectory(cacheRoot / kConfigurationName)
    , SearchPaths(std::move(searchPaths))
    , Options(std::move(options))
    , OptionsHash(HashOptions(Options))
    , Slang(std::make_unique<SlangState>())
    {
        // Drop empty entries and make the rest absolute, so module lookup does not depend on the working directory.
        std::erase_if(SearchPaths, [](const std::filesystem::path& path) { return path.empty(); });
        for (std::filesystem::path& searchPath : SearchPaths)
        {
            std::error_code errorCode;
            const std::filesystem::path absolutePath = std::filesystem::absolute(searchPath, errorCode);
            if (!errorCode) searchPath = absolutePath.lexically_normal();
        }
    }

    ShaderCompiler::~ShaderCompiler() = default;

    bool ShaderCompiler::CanCompile()
    {
#if defined(EOS_SHADER_TOOLS)
        return true;
#else
        return false;
#endif
    }

    bool ShaderCompiler::ReadCache(const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError) const
    {
        return ShaderCache::Read(ShaderCache::GetFilePath(CacheDirectory, description), OptionsHash, description, outProgram, outError);
    }

    bool ShaderCompiler::IsCacheUpToDate(const ShaderProgramDescription& description) const
    {
        CompiledShaderProgram cachedProgram;
        std::string error;
        if (!ReadCache(description, cachedProgram, error)) return false;

#if defined(EOS_SHADER_TOOLS)
        return cachedProgram.CompilerVersion == spGetBuildTagString() && ShaderCache::AreDependenciesUpToDate(cachedProgram);
#else
        return true;
#endif
    }

    std::shared_ptr<const CompiledShaderProgram> ShaderCompiler::LoadProgram(const ShaderProgramDescription& description, std::string& outDiagnostics)
    {
        const uint64_t key = ShaderCache::HashDescription(description);
        if (const auto it = LoadedPrograms.find(key); it != LoadedPrograms.end() && it->second->Description == description)
        {
            return it->second;
        }

        CompiledShaderProgram cachedProgram;
        std::string cacheError;
        const bool hasCache = ReadCache(description, cachedProgram, cacheError);

#if defined(EOS_SHADER_TOOLS)
        if (hasCache && cachedProgram.CompilerVersion == spGetBuildTagString() && ShaderCache::AreDependenciesUpToDate(cachedProgram))
        {
            auto program = std::make_shared<const CompiledShaderProgram>(std::move(cachedProgram));
            LoadedPrograms[key] = program;
            return program;
        }

        return CompileProgram(description, outDiagnostics);
#else
        if (!hasCache)
        {
            outDiagnostics += "error: " + cacheError + ". This build cannot compile shaders (EOS_SHADER_TOOLS is off); run EOSShaderCompilerTool to fill the cache.\n";
            return nullptr;
        }

        auto program = std::make_shared<const CompiledShaderProgram>(std::move(cachedProgram));
        LoadedPrograms[key] = program;
        return program;
#endif
    }

    std::shared_ptr<const CompiledShaderProgram> ShaderCompiler::CompileProgram([[maybe_unused]] const ShaderProgramDescription& description, std::string& outDiagnostics)
    {
#if defined(EOS_SHADER_TOOLS)
        slang::ISession* session = Slang->GetSession(Options, SearchPaths, description.Defines, outDiagnostics);
        if (!session) return nullptr;

        Slang::ComPtr<ISlangBlob> diagnostics;
        slang::IModule* module = session->loadModule(description.Module.c_str(), diagnostics.writeRef());
        AppendDiagnostics(outDiagnostics, diagnostics);
        if (!module)
        {
            std::string searched;
            for (const std::filesystem::path& searchPath : SearchPaths) searched += "\n    " + searchPath.string();
            outDiagnostics += "error: could not load shader module '" + description.Module + "'. Searched:" + searched + "\n";
            return nullptr;
        }

        std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
        if (description.EntryPoints.empty())
        {
            for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); ++i)
            {
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                if (SLANG_SUCCEEDED(module->getDefinedEntryPoint(i, entryPoint.writeRef())) && entryPoint) entryPoints.push_back(entryPoint);
            }
        }
        else
        {
            for (const std::string& name : description.EntryPoints)
            {
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                if (SLANG_FAILED(module->findEntryPointByName(name.c_str(), entryPoint.writeRef())) || !entryPoint)
                {
                    outDiagnostics += "error: module '" + description.Module + "' has no entry point '" + name + "' marked with [shader(\"...\")]. Entry points: " + ListDefinedEntryPoints(module) + "\n";
                    return nullptr;
                }

                entryPoints.push_back(entryPoint);
            }
        }

        auto program = std::make_shared<CompiledShaderProgram>();
        program->Description = description;
        program->CompilerVersion = spGetBuildTagString();
        program->EntryPoints.resize(entryPoints.size());

        // Every source file the module was built from, including imported modules, for staleness checks and hot reload.
        std::set<std::filesystem::path> dependencyPaths;
        for (SlangInt32 i = 0; i < module->getDependencyFileCount(); ++i)
        {
            if (const char* path = module->getDependencyFilePath(i)) dependencyPaths.insert(std::filesystem::absolute(FromUtf8(path)).lexically_normal());
        }

        for (const std::filesystem::path& path : dependencyPaths)
        {
            ShaderSourceDependency dependency{.Path = path};
            if (!ShaderCache::HashFile(path, dependency.ContentHash))
            {
                outDiagnostics += "warning: could not read shader dependency " + path.string() + "; the cache for '" + description.Module + "' will be rebuilt every time.\n";
            }
            program->Dependencies.push_back(std::move(dependency));
        }

        // A module without entry points is still cached, so the build tool does not try it again on every run.
        if (!entryPoints.empty())
        {
            // Link all entry points together: shared imports are linked once, and each entry point still gets its own
            // SPIR-V module with everything it does not use stripped out.
            std::vector<slang::IComponentType*> components{module};
            for (const Slang::ComPtr<slang::IEntryPoint>& entryPoint : entryPoints) components.push_back(entryPoint);

            Slang::ComPtr<slang::IComponentType> composite;
            diagnostics.setNull();
            const SlangResult compositeResult = session->createCompositeComponentType(components.data(), static_cast<SlangInt>(components.size()), composite.writeRef(), diagnostics.writeRef());
            AppendDiagnostics(outDiagnostics, diagnostics);
            if (SLANG_FAILED(compositeResult) || !composite) return nullptr;

            Slang::ComPtr<slang::IComponentType> linkedProgram;
            diagnostics.setNull();
            const SlangResult linkResult = composite->link(linkedProgram.writeRef(), diagnostics.writeRef());
            AppendDiagnostics(outDiagnostics, diagnostics);
            if (SLANG_FAILED(linkResult) || !linkedProgram) return nullptr;

            for (size_t i = 0; i < program->EntryPoints.size(); ++i)
            {
                Slang::ComPtr<ISlangBlob> code;
                diagnostics.setNull();
                const SlangResult codeResult = linkedProgram->getEntryPointCode(static_cast<SlangInt>(i), 0, code.writeRef(), diagnostics.writeRef());
                AppendDiagnostics(outDiagnostics, diagnostics);
                if (SLANG_FAILED(codeResult) || !code || code->getBufferSize() == 0 || code->getBufferSize() % sizeof(uint32_t) != 0)
                {
                    outDiagnostics += "error: code generation failed for entry point " + std::to_string(i) + " of module '" + description.Module + "'.\n";
                    return nullptr;
                }

                const auto* words = static_cast<const uint32_t*>(code->getBufferPointer());
                program->EntryPoints[i].Spirv.assign(words, words + code->getBufferSize() / sizeof(uint32_t));
            }

            if (!SlangReflection::Reflect(linkedProgram, *program, outDiagnostics)) return nullptr;
        }

        std::string cacheError;
        if (!ShaderCache::Write(ShaderCache::GetFilePath(CacheDirectory, description), OptionsHash, *program, cacheError))
        {
            outDiagnostics += "warning: " + cacheError + "\n";
        }

        LoadedPrograms[ShaderCache::HashDescription(description)] = program;
        return program;
#else
        outDiagnostics += "error: cannot compile '" + description.Module + "': this build has no shader compiler (EOS_SHADER_TOOLS is off).\n";
        return nullptr;
#endif
    }

    void ShaderCompiler::ResetModuleCache()
    {
#if defined(EOS_SHADER_TOOLS)
        Slang->Sessions.clear();
#endif
    }
}
