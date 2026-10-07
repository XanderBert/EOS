// EOSGraphTool: turns a render graph file (YAML) into the C++ header that builds without EOS_GRAPH_TOOLS use instead.
//
//     EOSGraphTool <graph.yaml> <header.h>
//
// The header defines a GraphFileDescription named after the file: depthOfField.yaml becomes DepthOfFieldGraph.
// Mistakes in the file's shape are reported like compiler errors and fail the build; whether its pass types, pins and
// properties exist is checked when the application loads it, where the pass types are registered.

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "GraphTools/graphFileParser.h"

namespace
{
    // depthOfField -> DepthOfFieldGraph; characters that cannot be in a C++ name start a new word.
    [[nodiscard]] std::string VariableName(const std::string& stem)
    {
        std::string name;
        bool startWord = true;
        for (const char c : stem)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)))
            {
                startWord = true;
                continue;
            }
            name += startWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            startWord = false;
        }
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) name.insert(0, "Graph");
        return name + "Graph";
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: EOSGraphTool <graph.yaml> <header.h>\n");
        return 2;
    }

    const std::filesystem::path input = argv[1];
    const std::filesystem::path output = argv[2];

    const EOS::ParsedGraphFile file = EOS::ParseGraphFile(input);
    if (!file.Errors.empty())
    {
        for (const std::string& error : file.Errors) std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    std::error_code error;
    std::filesystem::create_directories(output.parent_path(), error);
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream << EOS::GenerateGraphHeader(file, VariableName(input.stem().string()));
    if (!stream)
    {
        std::fprintf(stderr, "%s: cannot be written\n", output.string().c_str());
        return 1;
    }
    return 0;
}
