#pragma once

#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "graphFileDescription.h"

// Reads render graph files (YAML) into a GraphFileDescription, and writes the C++ header builds without
// EOS_GRAPH_TOOLS use instead. Only built with EOS_GRAPH_TOOLS: EOS uses it to load and reload graph files at runtime,
// EOSGraphTool to generate the headers.
//
// Parsing checks the shape of the file (a map of passes with settings, a list of "From -> To" edges). Whether the pass
// types, pins and properties exist is only known at runtime, where the pass types are registered.
namespace EOS
{
    /**
     * @brief A parsed graph file and the storage its description points into.
     */
    struct ParsedGraphFile final
    {
        ParsedGraphFile() = default;
        ParsedGraphFile(ParsedGraphFile&&) = default;
        ParsedGraphFile& operator=(ParsedGraphFile&&) = default;
        ParsedGraphFile(const ParsedGraphFile&) = delete;
        ParsedGraphFile& operator=(const ParsedGraphFile&) = delete;

        // "path:line:column: message", like a compiler's. The description is only complete without errors.
        std::vector<std::string> Errors;

        std::string Path;
        std::deque<std::string> Strings;        // a deque keeps them in place as it grows
        std::vector<GraphPassEntry> Passes;
        std::vector<GraphSetting> Settings;
        std::vector<const char*> Values;
        std::vector<GraphEdgeEntry> Edges;

        [[nodiscard]] GraphFileDescription Description() const
        {
            return {.Path = Path.c_str(), .Passes = Passes, .Settings = Settings, .Values = Values, .Edges = Edges};
        }
    };

    [[nodiscard]] ParsedGraphFile ParseGraphFile(const std::filesystem::path& path);

    /**
     * @brief The C++ header that defines a graph file as constants: a GraphFileDescription named variableName.
     */
    [[nodiscard]] std::string GenerateGraphHeader(const ParsedGraphFile& file, std::string_view variableName);
}
