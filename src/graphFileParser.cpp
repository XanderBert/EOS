#include "graphFileParser.h"

#include <ryml.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace EOS
{
    namespace
    {
        [[nodiscard]] std::string_view View(ryml::csubstr text)
        {
            return {text.str, text.len};
        }

        [[nodiscard]] std::string_view Trim(std::string_view text)
        {
            const size_t first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos) return {};
            const size_t last = text.find_last_not_of(" \t");
            return text.substr(first, last - first + 1);
        }

        // rapidyaml reports errors through callbacks that must not return: they throw this, and parsing catches it.
        struct YamlError final
        {
            std::string Message;
            size_t Line = ryml::npos;
            size_t Column = ryml::npos;
        };

        [[noreturn]] void ThrowBasicError(ryml::csubstr message, const ryml::ErrorDataBasic& data, void*)
        {
            throw YamlError{.Message = std::string(View(message)), .Line = data.location.line, .Column = data.location.col};
        }

        [[noreturn]] void ThrowParseError(ryml::csubstr message, const ryml::ErrorDataParse& data, void*)
        {
            throw YamlError{.Message = std::string(View(message)), .Line = data.ymlloc.line, .Column = data.ymlloc.col};
        }

        [[noreturn]] void ThrowVisitError(ryml::csubstr message, const ryml::ErrorDataVisit&, void*)
        {
            throw YamlError{.Message = std::string(View(message))};
        }

        // Turns the YAML tree into the flat description, collecting every error rather than stopping at the first.
        class Parser final
        {
        public:
            Parser(const ryml::Tree& tree, const ryml::Parser& parser, ParsedGraphFile& out)
            : Tree(tree), YamlParser(parser), Out(out)
            {
            }

            void Parse()
            {
                const ryml::id_type root = Tree.root_id();
                if (!Tree.is_map(root))
                {
                    Error(root, "a graph file is a map with 'data', 'passes' and 'edges'");
                    return;
                }

                bool hasPasses = false;
                for (ryml::id_type node = Tree.first_child(root); node != ryml::NONE; node = Tree.next_sibling(node))
                {
                    const std::string_view key = View(Tree.key(node));
                    if (key == "data") ParseEntries(node, true);
                    else if (key == "passes")
                    {
                        hasPasses = true;
                        ParseEntries(node, false);
                    }
                    else if (key == "edges") ParseEdges(node);
                    else Error(node, std::format("unknown key '{}' (a graph file has 'data', 'passes' and 'edges')", key));
                }

                if (!hasPasses) Error(root, "the file has no 'passes'");
            }

        private:
            const ryml::Tree& Tree;
            const ryml::Parser& YamlParser;
            ParsedGraphFile& Out;

            [[nodiscard]] GraphSourceLocation LocationOf(ryml::id_type node) const
            {
                const ryml::Location location = Tree.location(YamlParser, node);
                if (location.line == ryml::npos) return {};
                return {.Line = static_cast<uint32_t>(location.line + 1), .Column = static_cast<uint32_t>(location.col + 1)};
            }

            void Error(ryml::id_type node, std::string_view message)
            {
                const GraphSourceLocation location = LocationOf(node);
                if (location.Line == 0) Out.Errors.push_back(std::format("{}: {}", Out.Path, message));
                else Out.Errors.push_back(std::format("{}:{}:{}: {}", Out.Path, location.Line, location.Column, message));
            }

            [[nodiscard]] const char* Intern(std::string_view text)
            {
                return Out.Strings.emplace_back(text).c_str();
            }

            [[nodiscard]] bool IsScalar(ryml::id_type node) const
            {
                return Tree.has_val(node) && !Tree.is_container(node);
            }

            // 'data' (data is named by its C++ type) or 'passes' (a pass by its shader, or by a C++ type that records).
            void ParseEntries(ryml::id_type section, bool data)
            {
                const std::string_view what = data ? "data" : "a pass";
                if (!Tree.is_map(section))
                {
                    if (data) Error(section, "'data' is a map from names to their settings: 'Camera: { type: flyCamera }'");
                    else Error(section, "'passes' is a map from pass names to their settings: 'Name: { shader: module }'");
                    return;
                }

                for (ryml::id_type node = Tree.first_child(section); node != ryml::NONE; node = Tree.next_sibling(node))
                {
                    const std::string_view name = View(Tree.key(node));
                    if (name.empty() || name.find('.') != std::string_view::npos || name.find("->") != std::string_view::npos)
                    {
                        Error(node, std::format("'{}' cannot name {}: names are not empty and have no '.' or '->'", name, what));
                        continue;
                    }
                    if (Tree.find_child(section, Tree.key(node)) != node)
                    {
                        Error(node, std::format("there is already {} named '{}'", what, name));
                        continue;
                    }
                    if (!Tree.is_map(node))
                    {
                        if (data) Error(node, std::format("data '{}' needs its settings, at least a type: '{}: {{ type: flyCamera }}'", name, name));
                        else Error(node, std::format("pass '{}' needs its settings, at least a shader: '{}: {{ shader: module }}'", name, name));
                        continue;
                    }

                    const ryml::id_type shader = Tree.find_child(node, "shader");
                    const ryml::id_type type = Tree.find_child(node, "type");
                    if (data && shader != ryml::NONE) Error(shader, "data is made on the CPU by a C++ type, not by a shader: a shader goes under 'passes'");
                    else if (data && type == ryml::NONE) Error(node, std::format("data '{}' has no type", name));
                    else if (!data && shader != ryml::NONE && type != ryml::NONE) Error(node, std::format("pass '{}' has a shader and a type: 'shader' names a Slang pass, 'type' a pass type registered in C++", name));
                    else if (!data && shader == ryml::NONE && type == ryml::NONE) Error(node, std::format("pass '{}' has no shader: '{}: {{ shader: module }}'", name, name));
                    else if (shader != ryml::NONE && !IsScalar(shader)) Error(shader, "a shader is a module name");
                    else if (type != ryml::NONE && !IsScalar(type)) Error(type, "a type is a name");

                    const uint32_t settingCount = static_cast<uint32_t>(Tree.num_children(node));
                    Out.Passes.push_back(
                    {
                        .Name = Intern(name),
                        .FirstSetting = static_cast<uint32_t>(Out.Settings.size()),
                        .SettingCount = settingCount,
                        .Data = data,
                        .Location = LocationOf(node),
                    });
                    ParseSettings(node, 1);
                }
            }

            // The children of a map, as settings contiguous in Out.Settings; maps nested in them get their own range.
            void ParseSettings(ryml::id_type map, uint32_t depth)
            {
                const uint32_t first = static_cast<uint32_t>(Out.Settings.size());
                Out.Settings.resize(first + Tree.num_children(map));

                uint32_t index = first;
                for (ryml::id_type node = Tree.first_child(map); node != ryml::NONE; node = Tree.next_sibling(node), ++index)
                {
                    const std::string_view key = View(Tree.key(node));
                    if (Tree.find_child(map, Tree.key(node)) != node) Error(node, std::format("'{}' is set twice", key));

                    GraphSetting setting{.Key = Intern(key), .FirstValue = static_cast<uint32_t>(Out.Values.size()), .Location = LocationOf(node)};
                    if (IsScalar(node))
                    {
                        Out.Values.push_back(Intern(View(Tree.val(node))));
                        setting.ValueCount = 1;
                    }
                    else if (Tree.is_seq(node))
                    {
                        setting.IsList = true;
                        for (ryml::id_type item = Tree.first_child(node); item != ryml::NONE; item = Tree.next_sibling(item))
                        {
                            if (!IsScalar(item))
                            {
                                Error(item, std::format("the items of '{}' are single values", key));
                                continue;
                            }
                            Out.Values.push_back(Intern(View(Tree.val(item))));
                            ++setting.ValueCount;
                        }
                    }
                    else if (Tree.is_map(node) && depth == 1)
                    {
                        setting.IsMap = true;
                        setting.FirstChild = static_cast<uint32_t>(Out.Settings.size());
                        setting.ChildCount = static_cast<uint32_t>(Tree.num_children(node));
                        ParseSettings(node, depth + 1);
                    }
                    else
                    {
                        Error(node, std::format("'{}' is a value, a list [a, b] or, for an output, a map {{ format: RGBA_F16 }}", key));
                    }

                    Out.Settings[index] = setting;
                }
            }

            void ParseEdges(ryml::id_type edges)
            {
                if (!Tree.is_seq(edges))
                {
                    Error(edges, "'edges' is a list of connections from an output to an input: '- Camera.view -> Geometry.view'");
                    return;
                }

                for (ryml::id_type node = Tree.first_child(edges); node != ryml::NONE; node = Tree.next_sibling(node))
                {
                    const std::string_view text = IsScalar(node) ? View(Tree.val(node)) : std::string_view{};
                    const size_t arrow = text.find("->");
                    const std::string_view from = arrow == std::string_view::npos ? std::string_view{} : Trim(text.substr(0, arrow));
                    const std::string_view to = arrow == std::string_view::npos ? std::string_view{} : Trim(text.substr(arrow + 2));
                    if (from.empty() || to.empty() || to.find("->") != std::string_view::npos)
                    {
                        Error(node, std::format("'{}' is not a connection from an output to an input: 'Camera.view -> Geometry.view'", text));
                        continue;
                    }

                    Out.Edges.push_back({.From = Intern(from), .To = Intern(to), .Location = LocationOf(node)});
                }
            }
        };
    }

    ParsedGraphFile ParseGraphFile(const std::filesystem::path& path)
    {
        ParsedGraphFile file;
        file.Path = path.generic_string();

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            file.Errors.push_back(std::format("{}: cannot be read", file.Path));
            return file;
        }
        std::stringstream contents;
        contents << stream.rdbuf();
        const std::string text = contents.str();

        try
        {
            ryml::Callbacks callbacks;
            callbacks.set_error_basic(&ThrowBasicError).set_error_parse(&ThrowParseError).set_error_visit(&ThrowVisitError);

            ryml::EventHandlerTree handler(callbacks);
            ryml::Parser yamlParser(&handler, ryml::ParserOptions().locations(true));
            ryml::Tree tree(callbacks);
            ryml::parse_in_arena(&yamlParser, ryml::csubstr(file.Path.data(), file.Path.size()), ryml::csubstr(text.data(), text.size()), &tree);

            Parser parser(tree, yamlParser, file);
            parser.Parse();
        }
        catch (const YamlError& error)
        {
            if (error.Line == ryml::npos) file.Errors.push_back(std::format("{}: YAML {}", file.Path, error.Message));
            else file.Errors.push_back(std::format("{}:{}:{}: YAML {} (where the parser noticed it; look at the lines before too)", file.Path, error.Line + 1, error.Column + 1, error.Message));
        }

        return file;
    }
}
