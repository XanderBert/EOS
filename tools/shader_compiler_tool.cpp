#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "shaders/shaderCompiler.h"

namespace
{
    struct Options final
    {
        std::filesystem::path projectShaderDir;
        std::filesystem::path engineShaderDir;
        std::filesystem::path outputDir;
    };

    [[noreturn]] void ExitProcess(const int exitCode)
    {
        std::cout.flush();
        std::cerr.flush();
        std::fflush(nullptr);
        std::_Exit(exitCode);
    }

    [[nodiscard]] bool ParseOptions(int argc, char** argv, Options& out)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--project-shaders" && i + 1 < argc)
            {
                out.projectShaderDir = argv[++i];
                continue;
            }
            if (arg == "--engine-shaders" && i + 1 < argc)
            {
                out.engineShaderDir = argv[++i];
                continue;
            }
            if (arg == "--output" && i + 1 < argc)
            {
                out.outputDir = argv[++i];
                continue;
            }

            std::cerr << "Usage: EOSShaderCompilerTool"
                      << " --project-shaders <dir>"
                      << " --engine-shaders <dir>"
                      << " --output <dir>\n";
            return false;
        }

        return !out.projectShaderDir.empty() && !out.engineShaderDir.empty() && !out.outputDir.empty();
    }

    [[nodiscard]] std::set<std::string> CollectShaderModulesFromDirectory(const std::filesystem::path& shaderDir)
    {
        std::set<std::string> modules;
        std::error_code errorCode;

        if (!std::filesystem::exists(shaderDir, errorCode) || errorCode)
        {
            return modules;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderDir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            if (entry.path().extension() != ".slang")
            {
                continue;
            }

            std::ifstream shaderFile(entry.path());
            if (!shaderFile.is_open())
            {
                continue;
            }

            bool hasEntryPoint = false;
            std::string line;
            while (std::getline(shaderFile, line))
            {
                if (line.find("[shader(\"") != std::string::npos)
                {
                    hasEntryPoint = true;
                    break;
                }
            }

            if (!hasEntryPoint)
            {
                continue;
            }

            const std::string moduleName = entry.path().stem().string();
            if (!moduleName.empty())
            {
                modules.insert(moduleName);
            }
        }

        return modules;
    }
}

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(true);
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    Options options;
    if (!ParseOptions(argc, argv, options))  ExitProcess(1);

    std::set<std::string> moduleNames = CollectShaderModulesFromDirectory(options.projectShaderDir);
    std::set<std::string> engineModules = CollectShaderModulesFromDirectory(options.engineShaderDir);
    moduleNames.insert(engineModules.begin(), engineModules.end());

    if (moduleNames.empty())
    {
        std::cout << "[shader-tool] no .slang files found in configured shader directories\n";
        ExitProcess(0);
    }

    std::cout << "[shader-tool] compiling " << moduleNames.size() << " modules\n";

    std::vector<std::string> searchPaths;
    searchPaths.reserve(2);
    searchPaths.push_back(options.engineShaderDir.string());
    searchPaths.push_back(options.projectShaderDir.string());

    EOS::ShaderCompiler compiler(options.outputDir, searchPaths);
    for (const std::string& moduleName : moduleNames)
    {
        std::cout << "[shader-tool] compiling module: " << moduleName << "\n";

        bool success = false;
        try
        {
            success = compiler.CompileAndCacheShader(moduleName.c_str());
        }
        catch (const std::bad_alloc&)
        {
            std::cerr << "[shader-tool][error] out-of-memory while compiling module: " << moduleName << "\n";
            ExitProcess(3);
        }
        catch (const std::exception& exception)
        {
            std::cerr << "[shader-tool][error] exception while compiling module: " << moduleName << " - " << exception.what() << "\n";
            ExitProcess(3);
        }
        catch (...)
        {
            std::cerr << "[shader-tool][error] unknown exception while compiling module: " << moduleName << "\n";
            ExitProcess(3);
        }

        if (!success)
        {
            std::cerr << "[shader-tool][error] failed to compile module: " << moduleName << "\n";
            ExitProcess(3);
        }
    }

    std::cout << "[shader-tool] finished successfully (compiled all modules)\n";
    ExitProcess(0);
}
