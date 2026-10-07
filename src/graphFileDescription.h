#pragma once

#include <cstdint>
#include <span>

// What a render graph file says, as plain data, before its pass types are looked up: passes with their settings, and
// edges. Builds generate it from every graph file into .generated/graphs/<file>.h, so a build without EOS_GRAPH_TOOLS
// uses graph files without reading or parsing YAML. With the tools, the YAML is parsed into the same form at runtime.
//
// Flat arrays with ranges into each other, so a generated header can define a graph file as constants.
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

    struct GraphFileDescription final
    {
        const char* Path = "";                  // the YAML file it comes from
        std::span<const GraphPassEntry> Passes{};
        std::span<const GraphSetting> Settings{};
        std::span<const char* const> Values{};
        std::span<const GraphEdgeEntry> Edges{};
    };
}
