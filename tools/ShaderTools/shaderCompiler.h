#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "defines.h"
#include "shaderTypes.h"

namespace EOS
{
    enum class ShaderOptimizationLevel : uint8_t
    {
        None = 0,
        Default,
        High,
        Maximal,
    };

    /**
     * @brief Settings that apply to every program a ShaderCompiler builds. They are part of the cache key, so a cache
     *        written with other options is treated as stale.
     */
    struct ShaderCompilerOptions final
    {
        bool DebugInfo = false;
        ShaderOptimizationLevel Optimization = ShaderOptimizationLevel::High;
        std::vector<ShaderMacro> GlobalDefines{};

        // Modules linked into every program, whether it imports them or not. eos.bindless is one: it provides the
        // getDescriptorFromHandle that maps DescriptorHandle<T> onto EOS's descriptor set, and a program that used a
        // DescriptorHandle without it would silently get Slang's default bindings instead.
        std::vector<std::string> LinkedModules{};

        // Debug builds of EOS: full debug info and no optimization, so shaders can be stepped through in RenderDoc / Nsight.
        // Other builds: optimized, without debug info.
        [[nodiscard]] static ShaderCompilerOptions Default();
    };

    /**
     * @brief Compiles Slang modules into CompiledShaderPrograms and keeps the on-disk shader cache up to date.
     *
     * Without EOS_SHADER_TOOLS, programs can only be loaded from the cache.
     * Not thread-safe: Slang sessions may only be used from one thread at a time.
     */
    class ShaderCompiler final
    {
    public:
        /**
         * @param cacheRoot Where compiled programs are stored. A subdirectory per build configuration is used inside it.
         * @param searchPaths Directories modules are looked up in, in order.
         * @param options Settings applied to every program.
         */
        ShaderCompiler(const std::filesystem::path& cacheRoot, std::vector<std::filesystem::path> searchPaths, ShaderCompilerOptions options = ShaderCompilerOptions::Default());
        ~ShaderCompiler();
        DELETE_COPY_MOVE(ShaderCompiler);

        /**
         * @brief Gets a program from memory, from the cache when the cache is up to date, or by compiling it.
         * @param description The program to load.
         * @param outDiagnostics Receives compiler errors and warnings. Warnings can be present on success.
         * @return The program, or nullptr when it could not be loaded or compiled.
         */
        [[nodiscard]] std::shared_ptr<const CompiledShaderProgram> LoadProgram(const ShaderProgramDescription& description, std::string& outDiagnostics);

        /**
         * @brief Compiles a program from source and writes it to the cache, whether or not the cache was up to date.
         * @note Modules Slang already loaded are reused. Call ResetModuleCache() first when sources changed on disk.
         */
        [[nodiscard]] std::shared_ptr<const CompiledShaderProgram> CompileProgram(const ShaderProgramDescription& description, std::string& outDiagnostics);

        /**
         * @brief True when the cached program was written with the current options and none of its sources changed since.
         */
        [[nodiscard]] bool IsCacheUpToDate(const ShaderProgramDescription& description) const;

        /**
         * @brief Drops every module Slang has loaded, so the next compile reads sources from disk again. Programs that were
         *        already loaded stay available.
         */
        void ResetModuleCache();

        [[nodiscard]] const std::filesystem::path& GetCacheDirectory() const { return CacheDirectory; }
        [[nodiscard]] const std::vector<std::filesystem::path>& GetSearchPaths() const { return SearchPaths; }

        /**
         * @brief Whether this build can compile shaders from source (EOS_SHADER_TOOLS), or only load cached ones.
         */
        [[nodiscard]] static bool CanCompile();

    private:
        [[nodiscard]] bool ReadCache(const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError) const;

        struct SlangState;

        std::filesystem::path CacheDirectory;
        std::vector<std::filesystem::path> SearchPaths;
        ShaderCompilerOptions Options;
        uint64_t OptionsHash = 0;

        // Programs loaded or compiled so far, keyed by ShaderCache::HashDescription.
        std::unordered_map<uint64_t, std::shared_ptr<const CompiledShaderProgram>> LoadedPrograms;

        std::unique_ptr<SlangState> Slang;
    };
}
