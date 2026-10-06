#include "shaderCompiler.h"

#include <algorithm>
#include <map>
#include <set>

#include "shaderCache.h"

#if defined(EOS_SHADER_TOOLS)
#include "shaderCodegen.h"
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

        // Below the cache directory: the precompiled library modules (ShaderCompiler::PrecompileModule).
        constexpr const char* kPrecompiledModuleDirectory = "modules";

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

            hash = ShaderCache::HashString("|linked|", hash);
            for (const std::string& linkedModule : options.LinkedModules) hash = ShaderCache::HashString(linkedModule, hash);

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
        return {.DebugInfo = true, .Optimization = ShaderOptimizationLevel::None, .LinkedModules = {"eos.bindless"}};
#else
        return {.DebugInfo = false, .Optimization = ShaderOptimizationLevel::High, .LinkedModules = {"eos.bindless"}};
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

        // Modules are named as in an import ("eos.imgui"), but loadModule takes the path below a search path ("eos/imgui").
        [[nodiscard]] std::string ToModulePath(const std::string& moduleName)
        {
            std::string modulePath = moduleName;
            std::ranges::replace(modulePath, '.', '/');
            return modulePath;
        }

        [[nodiscard]] slang::IModule* LoadModule(slang::ISession* session, const std::string& moduleName, const std::vector<std::filesystem::path>& searchPaths, std::string& outDiagnostics)
        {
            const std::string modulePath = ToModulePath(moduleName);

            Slang::ComPtr<ISlangBlob> diagnostics;
            slang::IModule* module = session->loadModule(modulePath.c_str(), diagnostics.writeRef());
            AppendDiagnostics(outDiagnostics, diagnostics);
            if (!module)
            {
                std::string searched;
                for (const std::filesystem::path& searchPath : searchPaths) searched += "\n    " + searchPath.string();
                outDiagnostics += "error: could not load shader module '" + moduleName + "'. Searched:" + searched + "\n";
            }

            return module;
        }

        void AddDependencies(slang::IModule* module, std::set<std::filesystem::path>& inOutPaths)
        {
            for (SlangInt32 i = 0; i < module->getDependencyFileCount(); ++i)
            {
                if (const char* path = module->getDependencyFilePath(i)) inOutPaths.insert(std::filesystem::absolute(FromUtf8(path)).lexically_normal());
            }
        }

        [[nodiscard]] std::vector<ShaderSourceDependency> HashDependencies(const std::set<std::filesystem::path>& paths, const std::string& moduleName, std::string& outDiagnostics)
        {
            std::vector<ShaderSourceDependency> dependencies;
            for (const std::filesystem::path& path : paths)
            {
                ShaderSourceDependency dependency{.Path = path};
                if (!ShaderCache::HashFile(path, dependency.ContentHash))
                {
                    outDiagnostics += "warning: could not read shader dependency " + path.string() + "; the cache for '" + moduleName + "' will be rebuilt every time.\n";
                }
                dependencies.push_back(std::move(dependency));
            }

            return dependencies;
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

        // Where precompiled library modules are looked for, in order, and the key they have to be written with.
        std::vector<std::filesystem::path> PrecompiledModuleDirectories;
        uint64_t PrecompiledModuleKey = 0;

        // Loads the up-to-date precompiled modules into a new session; importing them then skips parsing and checking.
        // The first directory holding a module wins. Stale ones are left out, so importing them compiles their source.
        void PreloadPrecompiledModules(slang::ISession* session, std::string& outDiagnostics) const
        {
            std::vector<std::pair<std::string, ShaderCache::DerivedEntry>> modules;
            std::set<std::string> moduleNames;
            for (const std::filesystem::path& directory : PrecompiledModuleDirectories)
            {
                std::error_code errorCode;
                for (const auto& file : std::filesystem::directory_iterator(directory, errorCode))
                {
                    if (!file.is_regular_file() || file.path().extension() != ShaderCache::FileExtension) continue;

                    std::string moduleName = file.path().stem().string();
                    ShaderCache::DerivedEntry entry;
                    if (moduleNames.contains(moduleName) || !ShaderCache::ReadDerived(file.path(), PrecompiledModuleKey, entry)) continue;

                    moduleNames.insert(moduleName);
                    modules.emplace_back(std::move(moduleName), std::move(entry));
                }
            }

            // Library modules share most of their sources (every one imports eos.core), so each file is hashed once.
            std::map<std::filesystem::path, uint64_t> fileHashes;
            const auto isUpToDate = [&](const ShaderCache::DerivedEntry& entry)
            {
                if (entry.Dependencies.empty()) return false;

                return std::ranges::all_of(entry.Dependencies, [&](const ShaderSourceDependency& dependency)
                {
                    auto [it, isNew] = fileHashes.try_emplace(dependency.Path, 0);
                    if (isNew && !ShaderCache::HashFile(dependency.Path, it->second)) it->second = ~dependency.ContentHash;
                    return it->second == dependency.ContentHash;
                });
            };
            std::erase_if(modules, [&](const auto& module) { return !isUpToDate(module.second); });

            // A module's dependencies include those of everything it imports, so loading the ones with fewer dependencies
            // first puts every import in place before the modules that use it.
            std::ranges::stable_sort(modules, {}, [](const auto& module) { return module.second.Dependencies.size(); });

            for (const auto& [moduleName, entry] : modules)
            {
                Slang::ComPtr<ISlangBlob> blob;
                blob.attach(slang_createBlob(entry.Payload.data(), entry.Payload.size()));

                Slang::ComPtr<ISlangBlob> diagnostics;
                if (!session->loadModuleFromIRBlob(ToModulePath(moduleName).c_str(), ToUtf8(entry.Source).c_str(), blob, diagnostics.writeRef()))
                {
                    outDiagnostics += "warning: could not load the precompiled module '" + moduleName + "'; it is compiled from source instead.\n";
                    AppendDiagnostics(outDiagnostics, diagnostics);
                }
            }
        }

        [[nodiscard]] slang::ISession* GetSession(const ShaderCompilerOptions& options, const std::vector<std::filesystem::path>& searchPaths, const std::vector<ShaderMacro>& defines, std::string& outDiagnostics)
        {
            uint64_t definesHash = 0;
            for (const ShaderMacro& define : defines)
            {
                definesHash = ShaderCache::HashString(define.Name, definesHash);
                definesHash = ShaderCache::HashString(define.Value, definesHash);
            }

            if (const auto it = Sessions.find(definesHash); it != Sessions.end()) return it->second;

            Slang::ComPtr<slang::ISession> session = CreateSession(options, searchPaths, defines, outDiagnostics);
            if (!session) return nullptr;

            return Sessions.emplace(definesHash, session).first->second;
        }

        // A session nothing else uses, for programs with modules from memory: a session keeps every module it loaded
        // by name, so the next version of a shader graph's module (same name, new source) would get the old one back.
        [[nodiscard]] Slang::ComPtr<slang::ISession> CreateSession(const ShaderCompilerOptions& options, const std::vector<std::filesystem::path>& searchPaths, const std::vector<ShaderMacro>& defines, std::string& outDiagnostics)
        {
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

            PreloadPrecompiledModules(session, outDiagnostics);
            return session;
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

#if defined(EOS_SHADER_TOOLS)
        Slang->PrecompiledModuleKey = GetPrecompiledModuleKey();
        Slang->PrecompiledModuleDirectories.push_back(CacheDirectory / kPrecompiledModuleDirectory);
        for (const std::filesystem::path& root : Options.LibraryCacheRoots)
        {
            Slang->PrecompiledModuleDirectories.push_back(root / kConfigurationName / kPrecompiledModuleDirectory);
        }
#endif
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
        Slang::ComPtr<slang::ISession> ownSession;
        slang::ISession* session = nullptr;
        if (description.SourceModules.empty()) session = Slang->GetSession(Options, SearchPaths, description.Defines, outDiagnostics);
        else session = ownSession = Slang->CreateSession(Options, SearchPaths, description.Defines, outDiagnostics);
        if (!session) return nullptr;

        // Modules from memory first, so Module and the files can import them by name.
        std::vector<slang::IModule*> sourceModules;
        std::set<std::string> sourceModulePaths;
        slang::IModule* module = nullptr;
        for (const ShaderSourceModule& sourceModule : description.SourceModules)
        {
            const std::string path = sourceModule.Name + ".slang";
            Slang::ComPtr<ISlangBlob> diagnostics;
            slang::IModule* loadedModule = session->loadModuleFromSourceString(ToModulePath(sourceModule.Name).c_str(), path.c_str(), sourceModule.Source.c_str(), diagnostics.writeRef());
            AppendDiagnostics(outDiagnostics, diagnostics);
            if (!loadedModule)
            {
                outDiagnostics += "error: could not compile the source module '" + sourceModule.Name + "'.\n";
                return nullptr;
            }

            sourceModulePaths.insert(path);
            if (sourceModule.Name == description.Module) module = loadedModule;
            else sourceModules.push_back(loadedModule);
        }

        if (!module) module = LoadModule(session, description.Module, SearchPaths, outDiagnostics);
        if (!module) return nullptr;

        // The modules every program links (ShaderCompilerOptions::LinkedModules), then this program's own.
        std::vector<slang::IModule*> linkedModules;
        std::vector<std::string> linkedModuleNames = Options.LinkedModules;
        linkedModuleNames.insert(linkedModuleNames.end(), description.LinkModules.begin(), description.LinkModules.end());
        for (const std::string& linkedModuleName : linkedModuleNames)
        {
            slang::IModule* linkedModule = LoadModule(session, linkedModuleName, SearchPaths, outDiagnostics);
            if (!linkedModule) return nullptr;
            if (linkedModule != module && std::ranges::find(linkedModules, linkedModule) == linkedModules.end()) linkedModules.push_back(linkedModule);
        }
        linkedModules.insert(linkedModules.end(), sourceModules.begin(), sourceModules.end());

        Slang::ComPtr<ISlangBlob> diagnostics;

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
        // Modules from memory have no file; they are part of the description, so a change makes it another program.
        std::set<std::filesystem::path> dependencyPaths;
        AddDependencies(module, dependencyPaths);
        for (slang::IModule* linkedModule : linkedModules) AddDependencies(linkedModule, dependencyPaths);
        std::erase_if(dependencyPaths, [&](const std::filesystem::path& path)
        {
            std::error_code errorCode;
            return sourceModulePaths.contains(path.filename().string()) && !std::filesystem::exists(path, errorCode);
        });

        program->Dependencies = HashDependencies(dependencyPaths, description.Module, outDiagnostics);

        // A module without entry points is still cached, so the build tool does not try it again on every run.
        if (!entryPoints.empty())
        {
            // Link all entry points together: shared imports are linked once, and each entry point still gets its own
            // SPIR-V module with everything it does not use stripped out.
            std::vector<slang::IComponentType*> components{module};
            components.insert(components.end(), linkedModules.begin(), linkedModules.end());
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

    std::filesystem::path ShaderCompiler::GetPrecompiledModulePath(const std::string& moduleName) const
    {
        return CacheDirectory / kPrecompiledModuleDirectory / (moduleName + ShaderCache::FileExtension);
    }

    uint64_t ShaderCompiler::GetPrecompiledModuleKey() const
    {
        return ShaderCache::HashString(GetCompilerVersion(), OptionsHash);
    }

    bool ShaderCompiler::IsPrecompiledModuleUpToDate(const std::string& moduleName) const
    {
        ShaderCache::DerivedEntry entry;
        return ShaderCache::ReadDerived(GetPrecompiledModulePath(moduleName), GetPrecompiledModuleKey(), entry) && ShaderCache::AreDependenciesUpToDate(entry.Dependencies);
    }

    bool ShaderCompiler::PrecompileModule([[maybe_unused]] const std::string& moduleName, std::string& outDiagnostics)
    {
#if defined(EOS_SHADER_TOOLS)
        slang::ISession* session = Slang->GetSession(Options, SearchPaths, {}, outDiagnostics);
        if (!session) return false;

        slang::IModule* module = LoadModule(session, moduleName, SearchPaths, outDiagnostics);
        if (!module) return false;

        Slang::ComPtr<ISlangBlob> blob;
        if (SLANG_FAILED(module->serialize(blob.writeRef())) || !blob)
        {
            outDiagnostics += "error: Slang could not serialize module '" + moduleName + "'.\n";
            return false;
        }

        std::set<std::filesystem::path> dependencyPaths;
        AddDependencies(module, dependencyPaths);

        const ShaderCache::DerivedEntry entry
        {
            .Key = GetPrecompiledModuleKey(),
            .Source = module->getFilePath() ? std::filesystem::absolute(FromUtf8(module->getFilePath())).lexically_normal() : std::filesystem::path{},
            .Dependencies = HashDependencies(dependencyPaths, moduleName, outDiagnostics),
            .Payload = std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()),
        };

        std::string error;
        if (!ShaderCache::WriteDerived(GetPrecompiledModulePath(moduleName), entry, error))
        {
            outDiagnostics += "error: " + error + "\n";
            return false;
        }

        return true;
#else
        outDiagnostics += "error: cannot precompile '" + moduleName + "': this build has no shader compiler (EOS_SHADER_TOOLS is off).\n";
        return false;
#endif
    }

    bool ShaderCompiler::GenerateCppHeader([[maybe_unused]] const std::string& moduleName, [[maybe_unused]] const std::vector<CppExportRoot>& roots, GeneratedCppHeader& outHeader, std::string& outDiagnostics)
    {
        outHeader = {};

#if defined(EOS_SHADER_TOOLS)
        slang::ISession* session = Slang->GetSession(Options, SearchPaths, {}, outDiagnostics);
        if (!session) return false;

        slang::IModule* module = LoadModule(session, moduleName, SearchPaths, outDiagnostics);
        if (!module) return false;

        std::set<std::filesystem::path> dependencyPaths;
        AddDependencies(module, dependencyPaths);
        outHeader.Dependencies = HashDependencies(dependencyPaths, moduleName, outDiagnostics);

        return ShaderCodegen::GenerateHeader(session, module, moduleName, roots, outHeader.Text, outDiagnostics);
#else
        outDiagnostics += "error: cannot generate C++ for '" + moduleName + "': this build has no shader compiler (EOS_SHADER_TOOLS is off).\n";
        return false;
#endif
    }

    std::string ShaderCompiler::GetCompilerVersion()
    {
#if defined(EOS_SHADER_TOOLS)
        return spGetBuildTagString();
#else
        return {};
#endif
    }
}
