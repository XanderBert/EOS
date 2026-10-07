#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Reads render graph files (YAML) into a GraphFileDescription: what the file says, as plain data, before its pass types
// are looked up. GraphFile uses it to load and reload graph files.
//
// Parsing checks the shape of the file (a map of passes with settings, a list of "From -> To" edges). Whether the pass
// types, pins and properties exist is checked when the file is resolved against the registered pass types.
namespace EOS
{
    // 1-based, for messages; 0 when unknown.
    struct GraphSourceLocation final
    {
        uint32_t Line = 0;
        uint32_t Column = 0;
    };

    // "key: value", "key: [a, b, c]" (IsList) or "key: { a: 1, b: 2 }" (IsMap, children in GraphFileDescription::Settings).
    struct GraphSetting final
    {
        const char* Key = "";
        uint32_t FirstValue = 0;                // into GraphFileDescription::Values
        uint32_t ValueCount = 0;
        uint32_t FirstChild = 0;                // into GraphFileDescription::Settings
        uint32_t ChildCount = 0;
        bool IsList = false;
        bool IsMap = false;
        GraphSourceLocation Location{};
    };

    struct GraphPassEntry final
    {
        const char* Name = "";
        uint32_t FirstSetting = 0;              // its type, enabled, properties and output settings
        uint32_t SettingCount = 0;
        GraphSourceLocation Location{};
    };

    // "From -> To": "Pass.pin" or the name of an application resource on either side.
    struct GraphEdgeEntry final
    {
        const char* From = "";
        const char* To = "";
        GraphSourceLocation Location{};
    };

    // Flat arrays with ranges into each other.
    struct GraphFileDescription final
    {
        const char* Path = "";                  // the YAML file it comes from
        std::span<const GraphPassEntry> Passes{};
        std::span<const GraphSetting> Settings{};
        std::span<const char* const> Values{};
        std::span<const GraphEdgeEntry> Edges{};
    };

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
}
