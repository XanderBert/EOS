#include "renderGraphFile.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <deque>
#include <filesystem>
#include <ranges>
#include <set>
#include <string>
#include <system_error>

#include "logger.h"

#if defined(EOS_GRAPH_TOOLS)
#include "GraphTools/graphFileParser.h"
#endif

namespace EOS
{
    namespace
    {
        constexpr uint32_t kNone = 0xFFFFFFFF;

        struct FormatName final
        {
            std::string_view Name;
            Format Value;
        };

        // The formats a graph file can give an output, by their EOS::Format names.
        constexpr FormatName kFormatNames[] =
        {
            {"swapchain", Format::Invalid},
            {"R_UN8", Format::R_UN8}, {"R_UI16", Format::R_UI16}, {"R_UI32", Format::R_UI32}, {"R_UN16", Format::R_UN16}, {"R_F16", Format::R_F16}, {"R_F32", Format::R_F32},
            {"RG_UN8", Format::RG_UN8}, {"RG_UI16", Format::RG_UI16}, {"RG_UI32", Format::RG_UI32}, {"RG_UN16", Format::RG_UN16}, {"RG_F16", Format::RG_F16}, {"RG_F32", Format::RG_F32},
            {"RGBA_UN8", Format::RGBA_UN8}, {"RGBA_UI32", Format::RGBA_UI32}, {"RGBA_F16", Format::RGBA_F16}, {"RGBA_F32", Format::RGBA_F32},
            {"RGBA_SRGB8", Format::RGBA_SRGB8}, {"BGRA_UN8", Format::BGRA_UN8}, {"BGRA_SRGB8", Format::BGRA_SRGB8},
            {"Z_UN16", Format::Z_UN16}, {"Z_UN24", Format::Z_UN24}, {"Z_F32", Format::Z_F32}, {"Z_UN24_S_UI8", Format::Z_UN24_S_UI8}, {"Z_F32_S_UI8", Format::Z_F32_S_UI8},
        };

        [[nodiscard]] constexpr bool IsDepthFormat(Format format)
        {
            return format == Format::Z_UN16 || format == Format::Z_UN24 || format == Format::Z_F32 || format == Format::Z_UN24_S_UI8 || format == Format::Z_F32_S_UI8;
        }

        [[nodiscard]] constexpr bool IsInput(PinDirection direction) { return direction != PinDirection::Output; }
        [[nodiscard]] constexpr bool IsOutput(PinDirection direction) { return direction != PinDirection::Input; }

        [[nodiscard]] constexpr const char* ToString(PropertyType type)
        {
            switch (type)
            {
                case PropertyType::Bool:   return "a bool";
                case PropertyType::Int:    return "an int";
                case PropertyType::Float:  return "a float";
                case PropertyType::Float2: return "a float2";
                case PropertyType::Float3: return "a float3";
                case PropertyType::Float4: return "a float4";
                case PropertyType::Choice: return "a choice";
            }
            return "?";
        }

        [[nodiscard]] constexpr uint32_t ComponentCount(PropertyType type)
        {
            switch (type)
            {
                case PropertyType::Float2: return 2;
                case PropertyType::Float3: return 3;
                case PropertyType::Float4: return 4;
                default:                   return 1;
            }
        }

        [[nodiscard]] std::string_view Trim(std::string_view text)
        {
            const size_t first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos) return {};
            const size_t last = text.find_last_not_of(" \t");
            return text.substr(first, last - first + 1);
        }

        [[nodiscard]] bool IsIdentifier(std::string_view text)
        {
            return !text.empty() && std::ranges::all_of(text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
        }
    }

    // -------------------------------------------------------------------------------------------------------------------
    // Storage

    struct PassRegistryData final
    {
        struct Pin final
        {
            const char* Name = "";
            PinDirection Direction = PinDirection::Input;
            PinUsage Usage = PinUsage::Sampled;
            bool Optional = false;
            Format TextureFormat = Format::Invalid;
            size_t BufferSize = 0;
            float Scale = 1.0f;
            uint32_t SizeOf = kNone;                // pin index within the type
            uint32_t BypassFrom = kNone;            // pin index within the type
            std::array<float, 4> ClearColor{};
            float ClearDepth = 1.0f;
            uint8_t LayerCount = 1;
        };

        struct Property final
        {
            const char* Name = "";
            PropertyType Type = PropertyType::Float;
            PropertyValue Default{};
            float Min = 0.0f;
            float Max = 0.0f;
            uint32_t FirstChoice = 0;
            uint32_t ChoiceCount = 0;
        };

        struct Type final
        {
            const char* Name = "";
            PassKind Kind = PassKind::Compute;
            uint32_t FirstPin = 0;
            uint32_t PinCount = 0;
            uint32_t FirstProperty = 0;
            uint32_t PropertyCount = 0;
            PassTypeFunction Execute;
        };

        std::deque<std::string> Strings;            // every name; a deque keeps them in place as it grows
        std::vector<Type> Types;
        std::vector<Pin> Pins;
        std::vector<Property> Properties;
        std::vector<const char*> Choices;

        [[nodiscard]] const char* Intern(std::string_view text)
        {
            return Strings.emplace_back(text).c_str();
        }

        [[nodiscard]] uint32_t FindType(std::string_view name) const
        {
            for (uint32_t i = 0; i < Types.size(); ++i)
            {
                if (name == Types[i].Name) return i;
            }
            return kNone;
        }

        // Index within the type.
        [[nodiscard]] uint32_t FindPin(const Type& type, std::string_view name) const
        {
            for (uint32_t i = 0; i < type.PinCount; ++i)
            {
                if (name == Pins[type.FirstPin + i].Name) return i;
            }
            return kNone;
        }

        [[nodiscard]] uint32_t FindProperty(const Type& type, std::string_view name) const
        {
            for (uint32_t i = 0; i < type.PropertyCount; ++i)
            {
                if (name == Properties[type.FirstProperty + i].Name) return i;
            }
            return kNone;
        }

        [[nodiscard]] std::span<const char* const> GetChoices(const Property& property) const
        {
            return {Choices.data() + property.FirstChoice, property.ChoiceCount};
        }

        // "a, b, c", for error messages that list what exists.
        [[nodiscard]] std::string ListPins(const Type& type, bool inputs, bool outputs) const
        {
            std::string list;
            for (uint32_t i = 0; i < type.PinCount; ++i)
            {
                const Pin& pin = Pins[type.FirstPin + i];
                if ((inputs && IsInput(pin.Direction)) || (outputs && IsOutput(pin.Direction)))
                {
                    if (!list.empty()) list += ", ";
                    list += pin.Name;
                }
            }
            return list.empty() ? "none" : list;
        }
    };

    struct GraphFileData final
    {
        // One per pin of every pass.
        struct Slot final
        {
            uint32_t SourceSlot = kNone;            // the output that feeds this input
            uint32_t SourceResource = kNone;        // or the application resource that does
            uint32_t TargetResource = kNone;        // an output written into an application resource
            Format FormatOverride = Format::Invalid;
            bool HasFormatOverride = false;
            float ScaleOverride = 0.0f;             // 0: none
            const char* DebugName = "";             // "Pass.pin"
        };

        struct Pass final
        {
            const char* Name = "";
            uint32_t Type = kNone;
            bool Enabled = true;
            uint32_t FirstSlot = 0;
            uint32_t FirstValue = 0;
        };

        // A version of the file without errors. Reloading replaces it as a whole.
        struct Version final
        {
            std::deque<std::string> Strings;
            std::vector<Pass> Passes;
            std::vector<Slot> Slots;
            std::vector<PropertyValue> Values;
            std::vector<uint32_t> Order;            // passes, each after the passes it reads from
            std::vector<const char*> Resources;     // application resources the file connects to

            [[nodiscard]] const char* Intern(std::string_view text)
            {
                return Strings.emplace_back(text).c_str();
            }

            [[nodiscard]] uint32_t FindPass(std::string_view name) const
            {
                for (uint32_t i = 0; i < Passes.size(); ++i)
                {
                    if (name == Passes[i].Name) return i;
                }
                return kNone;
            }
        };

        // A resource resolved for this frame.
        struct Resource final
        {
            GraphTexture Texture{};
            GraphBuffer Buffer{};
            uint32_t DisabledBy = kNone;            // when missing: the disabled pass that would have written it

            [[nodiscard]] bool Valid() const { return Texture.Valid() || Buffer.Valid(); }
        };

        GraphFileData(const PassRegistryData& registry, const GraphFileDescription& compiled) : Registry(registry), Compiled(compiled) {}

        const PassRegistryData& Registry;
        GraphFileDescription Compiled;              // the version built into the application
#if defined(EOS_GRAPH_TOOLS)
        std::filesystem::file_time_type LastWriteTime{};
#endif
        std::unique_ptr<Version> Current;
        std::unique_ptr<Version> Pending;           // loaded by Reload(); replaces Current at the next AddTo

        // This frame.
        std::vector<Resource> SlotResources;
        std::vector<Resource> ApplicationResources;
        std::vector<uint32_t> SkippedBecauseOf;     // per pass: the disabled pass it needs output from

        // Problems found while adding passes are reported once per version of the file, not every frame.
        std::set<std::string> Reported;

        void ReportOnce(std::string message, bool error = true)
        {
            if (!Reported.insert(message).second) return;
            if (error) Logger->error("Render graph file {}: {}", Compiled.Path, message);
            else Logger->warn("Render graph file {}: {}", Compiled.Path, message);
        }

        // Puts description, resolved against the pass types, into Pending. Returns false (and reports why) on errors.
        bool Resolve(const GraphFileDescription& description);
#if defined(EOS_GRAPH_TOOLS)
        // Parses the YAML file the build's version came from, then resolves it.
        bool LoadFromDisk();
#endif
        void AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources);
        [[nodiscard]] uint32_t FindSlot(std::string_view pin) const;
    };

    // -------------------------------------------------------------------------------------------------------------------
    // PassRegistry

    PassRegistry::PassRegistry()
    : Types(std::make_unique<PassRegistryData>())
    {
    }

    PassRegistry::~PassRegistry() = default;

    void PassRegistry::Register(PassTypeDescription description)
    {
        PassRegistryData& registry = *Types;
        const std::string_view name = description.Name;

        CHECK_RETURN(!name.empty() && name.find('.') == std::string_view::npos, "Pass type '{}': names are not empty and have no '.'", name);
        CHECK_RETURN(registry.FindType(name) == kNone, "Pass type '{}' is registered twice", name);
        CHECK_RETURN(static_cast<bool>(description.Execute), "Pass type '{}' has no Execute function", name);

        const auto findPin = [&description](std::string_view pin) -> uint32_t
        {
            for (uint32_t i = 0; i < description.Pins.size(); ++i)
            {
                if (pin == description.Pins[i].Name) return i;
            }
            return kNone;
        };

        // Validate everything before storing anything, so a failed check in a release build registers nothing.
        for (uint32_t i = 0; i < description.Pins.size(); ++i)
        {
            const PinDescription& pin = description.Pins[i];
            const std::string_view pinName = pin.Name;
            const bool texture = !IsBufferUsage(pin.Usage);
            const bool target = pin.Usage == PinUsage::ColorTarget || pin.Usage == PinUsage::DepthTarget || pin.Usage == PinUsage::DepthTest;

            CHECK_RETURN(!pinName.empty() && pinName.find('.') == std::string_view::npos, "Pass type '{}': pin names are not empty and have no '.'", name);
            CHECK_RETURN(findPin(pinName) == i, "Pass type '{}' has two pins named '{}'", name, pinName);
            CHECK_RETURN(std::ranges::none_of(description.Properties, [pinName](const PropertyDescription& property) { return pinName == property.Name; }),
                         "Pass type '{}': '{}' is both a pin and a property", name, pinName);
            CHECK_RETURN(!target || description.Kind == PassKind::Raster, "Pass type '{}': only raster passes have targets (pin '{}')", name, pinName);
            CHECK_RETURN(description.Kind != PassKind::Transfer || pin.Usage == PinUsage::TransferBuffer, "Pass type '{}': transfer passes only have transfer buffers (pin '{}')", name, pinName);

            switch (pin.Usage)
            {
                case PinUsage::Sampled:
                case PinUsage::DepthTest:
                case PinUsage::ReadBuffer:
                case PinUsage::IndirectBuffer:
                    CHECK_RETURN(pin.Direction == PinDirection::Input, "Pass type '{}': pin '{}' only reads, so it is an input", name, pinName);
                    break;
                default:
                    break;
            }

            if (pin.Direction == PinDirection::Output)
            {
                if (texture)
                {
                    CHECK_RETURN(pin.Usage != PinUsage::DepthTarget || IsDepthFormat(pin.TextureFormat), "Pass type '{}': depth output '{}' needs a depth format", name, pinName);
                    CHECK_RETURN(pin.Usage == PinUsage::DepthTarget || !IsDepthFormat(pin.TextureFormat), "Pass type '{}': output '{}' is not a depth target but has a depth format", name, pinName);
                    CHECK_RETURN(pin.Output.Scale > 0.0f, "Pass type '{}': output '{}' needs a positive scale", name, pinName);
                }
                else
                {
                    CHECK_RETURN(pin.BufferSize > 0, "Pass type '{}': buffer output '{}' needs a size", name, pinName);
                }

                const std::string_view sizeOf = pin.Output.SizeOf;
                if (!sizeOf.empty())
                {
                    const uint32_t source = findPin(sizeOf);
                    CHECK_RETURN(texture && source != kNone && IsInput(description.Pins[source].Direction) && !IsBufferUsage(description.Pins[source].Usage),
                                 "Pass type '{}': output '{}' is sized like '{}', which is not a texture input", name, pinName, sizeOf);
                }

                const std::string_view bypass = pin.Output.BypassFrom;
                if (!bypass.empty())
                {
                    const uint32_t source = findPin(bypass);
                    CHECK_RETURN(source != kNone && IsInput(description.Pins[source].Direction) && IsBufferUsage(description.Pins[source].Usage) == !texture,
                                 "Pass type '{}': output '{}' bypasses to '{}', which is not an input of the same kind", name, pinName, bypass);
                }
            }
        }

        for (uint32_t i = 0; i < description.Properties.size(); ++i)
        {
            const PropertyDescription& property = description.Properties[i];
            const std::string_view propertyName = property.Name;

            CHECK_RETURN(IsIdentifier(propertyName) && propertyName != "type" && propertyName != "enabled",
                         "Pass type '{}': property '{}' needs a name of letters, digits and '_' other than 'type' and 'enabled'", name, propertyName);
            CHECK_RETURN(std::ranges::count_if(description.Properties, [propertyName](const PropertyDescription& other) { return propertyName == other.Name; }) == 1,
                         "Pass type '{}' has two properties named '{}'", name, propertyName);
            CHECK_RETURN(property.Type != PropertyType::Choice || (!property.Choices.empty() && property.Default.Int >= 0 && property.Default.Int < static_cast<int32_t>(property.Choices.size())),
                         "Pass type '{}': choice '{}' needs choices and a default among them", name, propertyName);
        }

        PassRegistryData::Type type
        {
            .Name = registry.Intern(name),
            .Kind = description.Kind,
            .FirstPin = static_cast<uint32_t>(registry.Pins.size()),
            .PinCount = static_cast<uint32_t>(description.Pins.size()),
            .FirstProperty = static_cast<uint32_t>(registry.Properties.size()),
            .PropertyCount = static_cast<uint32_t>(description.Properties.size()),
            .Execute = std::move(description.Execute),
        };

        for (const PinDescription& pin : description.Pins)
        {
            registry.Pins.push_back(
            {
                .Name = registry.Intern(pin.Name),
                .Direction = pin.Direction,
                .Usage = pin.Usage,
                .Optional = pin.Optional && pin.Direction == PinDirection::Input,
                .TextureFormat = pin.TextureFormat,
                .BufferSize = pin.BufferSize,
                .Scale = pin.Output.Scale,
                .SizeOf = std::string_view(pin.Output.SizeOf).empty() ? kNone : findPin(pin.Output.SizeOf),
                .BypassFrom = std::string_view(pin.Output.BypassFrom).empty() ? kNone : findPin(pin.Output.BypassFrom),
                .ClearColor = pin.Output.ClearColor,
                .ClearDepth = pin.Output.ClearDepth,
                .LayerCount = std::max<uint8_t>(pin.Output.LayerCount, 1),
            });
        }

        for (const PropertyDescription& property : description.Properties)
        {
            registry.Properties.push_back(
            {
                .Name = registry.Intern(property.Name),
                .Type = property.Type,
                .Default = property.Default,
                .Min = property.Min,
                .Max = property.Max,
                .FirstChoice = static_cast<uint32_t>(registry.Choices.size()),
                .ChoiceCount = static_cast<uint32_t>(property.Choices.size()),
            });

            for (const char* choice : property.Choices) registry.Choices.push_back(registry.Intern(choice));
        }

        registry.Types.push_back(std::move(type));
    }

    // -------------------------------------------------------------------------------------------------------------------
    // Resolving a graph file against the registered pass types

    namespace
    {
        // Turns a graph file's description into a Version: looks up its pass types, pins and properties, checks the
        // connections and orders the passes. Collects every error rather than stopping at the first.
        class Resolver final
        {
        public:
            Resolver(const PassRegistryData& registry, const GraphFileDescription& file, GraphFileData::Version& out)
            : Registry(registry), File(file), Out(out)
            {
            }

            std::vector<std::string> Errors;

            void Resolve()
            {
                for (const GraphPassEntry& pass : File.Passes) ResolvePass(pass);
                for (const GraphEdgeEntry& edge : File.Edges) ResolveEdge(edge);
                if (!Errors.empty()) return;

                CheckConnections();
                if (!Errors.empty()) return;

                Order();
            }

        private:
            struct Endpoint final
            {
                uint32_t Pass = kNone;
                uint32_t Pin = kNone;               // within the pass's type
                uint32_t Resource = kNone;          // an application resource instead of a pin
                bool Valid = false;
            };

            const PassRegistryData& Registry;
            const GraphFileDescription& File;
            GraphFileData::Version& Out;

            std::vector<GraphSourceLocation> PassLocations;         // per pass
            std::vector<std::string_view> FailedPasses;             // reported already; edges naming them are skipped quietly
            std::vector<GraphSourceLocation> SlotEdges;             // per slot: the edge that connected it (line 0: none)
            std::vector<std::pair<uint32_t, uint32_t>> Dependencies;    // (before, after) passes

            void Error(const GraphSourceLocation& location, std::string_view message)
            {
                if (location.Line == 0) Errors.push_back(fmt::format("{}: {}", File.Path, message));
                else Errors.push_back(fmt::format("{}:{}:{}: {}", File.Path, location.Line, location.Column, message));
            }

            [[nodiscard]] const PassRegistryData::Type& TypeOf(uint32_t pass) const
            {
                return Registry.Types[Out.Passes[pass].Type];
            }

            [[nodiscard]] const PassRegistryData::Pin& PinOf(uint32_t pass, uint32_t pin) const
            {
                return Registry.Pins[TypeOf(pass).FirstPin + pin];
            }

            [[nodiscard]] uint32_t SlotOf(uint32_t pass, uint32_t pin) const
            {
                return Out.Passes[pass].FirstSlot + pin;
            }

            [[nodiscard]] std::string Describe(const Endpoint& endpoint) const
            {
                if (endpoint.Resource != kNone) return Out.Resources[endpoint.Resource];
                return fmt::format("{}.{}", Out.Passes[endpoint.Pass].Name, PinOf(endpoint.Pass, endpoint.Pin).Name);
            }

            [[nodiscard]] std::span<const GraphSetting> Children(const GraphSetting& setting) const
            {
                return File.Settings.subspan(setting.FirstChild, setting.ChildCount);
            }

            [[nodiscard]] bool ReadSingle(const GraphSetting& setting, std::string_view& out)
            {
                if (setting.IsList || setting.IsMap || setting.ValueCount != 1)
                {
                    Error(setting.Location, fmt::format("'{}' is a single value", setting.Key));
                    return false;
                }
                out = File.Values[setting.FirstValue];
                return true;
            }

            [[nodiscard]] bool ReadBool(const GraphSetting& setting, bool& out)
            {
                std::string_view text;
                if (!ReadSingle(setting, text)) return false;
                if (text == "true") out = true;
                else if (text == "false") out = false;
                else
                {
                    Error(setting.Location, fmt::format("'{}' is true or false, not '{}'", setting.Key, text));
                    return false;
                }
                return true;
            }

            template<typename Number>
            [[nodiscard]] bool ParseNumber(const GraphSetting& setting, std::string_view text, Number& out)
            {
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
                if (error != std::errc{} || end != text.data() + text.size() || (std::is_floating_point_v<Number> && !std::isfinite(static_cast<double>(out))))
                {
                    Error(setting.Location, fmt::format("'{}' is {}, not '{}'", setting.Key, std::is_floating_point_v<Number> ? "a number" : "an integer", text));
                    return false;
                }
                return true;
            }

            void ResolvePass(const GraphPassEntry& entry)
            {
                const std::string_view name = entry.Name;
                const std::span<const GraphSetting> settings = File.Settings.subspan(entry.FirstSetting, entry.SettingCount);

                if (Out.FindPass(name) != kNone)
                {
                    Error(entry.Location, fmt::format("there is already a pass named '{}'", name));
                    return;
                }

                const auto typeSetting = std::ranges::find_if(settings, [](const GraphSetting& setting) { return std::string_view(setting.Key) == "type"; });
                std::string_view typeName;
                if (typeSetting == settings.end())
                {
                    Error(entry.Location, fmt::format("pass '{}' has no type", name));
                    FailedPasses.push_back(name);
                    return;
                }
                if (!ReadSingle(*typeSetting, typeName))
                {
                    FailedPasses.push_back(name);
                    return;
                }

                const uint32_t typeIndex = Registry.FindType(typeName);
                if (typeIndex == kNone)
                {
                    std::string known;
                    for (const PassRegistryData::Type& type : Registry.Types) known += fmt::format("{}{}", known.empty() ? "" : ", ", type.Name);
                    Error(typeSetting->Location, fmt::format("unknown pass type '{}' (registered: {})", typeName, known.empty() ? "none" : known));
                    FailedPasses.push_back(name);
                    return;
                }

                const PassRegistryData::Type& type = Registry.Types[typeIndex];
                const uint32_t passIndex = static_cast<uint32_t>(Out.Passes.size());
                Out.Passes.push_back(
                {
                    .Name = Out.Intern(name),
                    .Type = typeIndex,
                    .FirstSlot = static_cast<uint32_t>(Out.Slots.size()),
                    .FirstValue = static_cast<uint32_t>(Out.Values.size()),
                });
                PassLocations.push_back(entry.Location);

                for (uint32_t i = 0; i < type.PinCount; ++i)
                {
                    Out.Slots.push_back({.DebugName = Out.Intern(fmt::format("{}.{}", name, Registry.Pins[type.FirstPin + i].Name))});
                    SlotEdges.push_back({});
                }
                for (uint32_t i = 0; i < type.PropertyCount; ++i) Out.Values.push_back(Registry.Properties[type.FirstProperty + i].Default);

                GraphFileData::Pass& pass = Out.Passes[passIndex];
                for (const GraphSetting& setting : settings)
                {
                    const std::string_view key = setting.Key;
                    if (key == "type") continue;

                    if (key == "enabled")
                    {
                        bool enabled = true;
                        if (ReadBool(setting, enabled)) pass.Enabled = enabled;
                        continue;
                    }

                    if (const uint32_t property = Registry.FindProperty(type, key); property != kNone)
                    {
                        ResolveProperty(Registry.Properties[type.FirstProperty + property], setting, Out.Values[pass.FirstValue + property]);
                        continue;
                    }

                    if (const uint32_t pin = Registry.FindPin(type, key); pin != kNone)
                    {
                        ResolveOutputSettings(Registry.Pins[type.FirstPin + pin], setting, Out.Slots[pass.FirstSlot + pin]);
                        continue;
                    }

                    std::string properties;
                    for (uint32_t i = 0; i < type.PropertyCount; ++i) properties += fmt::format("{}{}", properties.empty() ? "" : ", ", Registry.Properties[type.FirstProperty + i].Name);
                    Error(setting.Location, fmt::format("pass type '{}' has no property or output '{}' (properties: {}; outputs: {})",
                                                        type.Name, key, properties.empty() ? "none" : properties, Registry.ListPins(type, false, true)));
                }
            }

            void ResolveProperty(const PassRegistryData::Property& property, const GraphSetting& setting, PropertyValue& value)
            {
                switch (property.Type)
                {
                    case PropertyType::Bool:
                    {
                        bool result = false;
                        if (ReadBool(setting, result)) value.Bool = result;
                        return;
                    }
                    case PropertyType::Int:
                    {
                        std::string_view text;
                        int32_t result = 0;
                        if (ReadSingle(setting, text) && ParseNumber(setting, text, result)) value.Int = result;
                        return;
                    }
                    case PropertyType::Float:
                    {
                        std::string_view text;
                        float result = 0.0f;
                        if (ReadSingle(setting, text) && ParseNumber(setting, text, result)) value.Float.x = result;
                        return;
                    }
                    case PropertyType::Float2:
                    case PropertyType::Float3:
                    case PropertyType::Float4:
                    {
                        const uint32_t count = ComponentCount(property.Type);
                        if (!setting.IsList || setting.ValueCount != count)
                        {
                            Error(setting.Location, fmt::format("'{}' is {} numbers: [{}]", property.Name, count, count == 2 ? "x, y" : count == 3 ? "x, y, z" : "x, y, z, w"));
                            return;
                        }

                        glm::vec4 result = value.Float;
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            if (!ParseNumber(setting, File.Values[setting.FirstValue + i], result[static_cast<glm::length_t>(i)])) return;
                        }
                        value.Float = result;
                        return;
                    }
                    case PropertyType::Choice:
                    {
                        std::string_view text;
                        if (!ReadSingle(setting, text)) return;

                        const std::span<const char* const> choices = Registry.GetChoices(property);
                        for (uint32_t i = 0; i < choices.size(); ++i)
                        {
                            if (text == choices[i])
                            {
                                value.Int = static_cast<int32_t>(i);
                                return;
                            }
                        }
                        Error(setting.Location, fmt::format("'{}' is one of: {}; not '{}'", property.Name, fmt::join(choices, ", "), text));
                        return;
                    }
                }
            }

            // "output: { format: RGBA_F16, scale: 0.5 }" for an output whose texture the graph creates.
            void ResolveOutputSettings(const PassRegistryData::Pin& pin, const GraphSetting& setting, GraphFileData::Slot& slot)
            {
                if (pin.Direction != PinDirection::Output || IsBufferUsage(pin.Usage))
                {
                    Error(setting.Location, fmt::format("'{}' is a pin; only texture outputs have settings here (format, scale)", pin.Name));
                    return;
                }
                if (!setting.IsMap)
                {
                    Error(setting.Location, fmt::format("the settings of output '{}' are a map: {{ format: RGBA_F16, scale: 0.5 }}", pin.Name));
                    return;
                }

                for (const GraphSetting& child : Children(setting))
                {
                    const std::string_view key = child.Key;
                    std::string_view text;
                    if (key == "format")
                    {
                        if (!ReadSingle(child, text)) continue;

                        const auto format = std::ranges::find(kFormatNames, text, &FormatName::Name);
                        if (format == std::end(kFormatNames))
                        {
                            Error(child.Location, fmt::format("unknown format '{}' (formats: {})", text, fmt::join(kFormatNames | std::views::transform(&FormatName::Name), ", ")));
                            continue;
                        }
                        if ((pin.Usage == PinUsage::DepthTarget) != IsDepthFormat(format->Value))
                        {
                            Error(child.Location, fmt::format("output '{}' {} a depth format", pin.Name, pin.Usage == PinUsage::DepthTarget ? "needs" : "cannot have"));
                            continue;
                        }
                        slot.FormatOverride = format->Value;
                        slot.HasFormatOverride = true;
                    }
                    else if (key == "scale")
                    {
                        float scale = 0.0f;
                        if (!ReadSingle(child, text) || !ParseNumber(child, text, scale)) continue;
                        if (scale <= 0.0f)
                        {
                            Error(child.Location, "a scale is larger than 0");
                            continue;
                        }
                        slot.ScaleOverride = scale;
                    }
                    else
                    {
                        Error(child.Location, fmt::format("unknown output setting '{}' (outputs have 'format' and 'scale')", key));
                    }
                }
            }

            [[nodiscard]] Endpoint ParseEndpoint(std::string_view text, const GraphSourceLocation& location)
            {
                const size_t dot = text.find('.');
                if (dot == std::string_view::npos)
                {
                    if (!IsIdentifier(text))
                    {
                        Error(location, fmt::format("'{}' is neither 'Pass.pin' nor the name of an application resource (letters, digits and '_')", text));
                        return {};
                    }

                    for (uint32_t i = 0; i < Out.Resources.size(); ++i)
                    {
                        if (text == Out.Resources[i]) return {.Resource = i, .Valid = true};
                    }
                    Out.Resources.push_back(Out.Intern(text));
                    return {.Resource = static_cast<uint32_t>(Out.Resources.size() - 1), .Valid = true};
                }

                const std::string_view passName = Trim(text.substr(0, dot));
                const std::string_view pinName = Trim(text.substr(dot + 1));
                const uint32_t pass = Out.FindPass(passName);
                if (pass == kNone)
                {
                    if (std::ranges::find(FailedPasses, passName) == FailedPasses.end()) Error(location, fmt::format("there is no pass named '{}'", passName));
                    return {};
                }

                const PassRegistryData::Type& type = TypeOf(pass);
                const uint32_t pin = Registry.FindPin(type, pinName);
                if (pin == kNone)
                {
                    Error(location, fmt::format("pass '{}' ({}) has no pin '{}' (pins: {})", passName, type.Name, pinName, Registry.ListPins(type, true, true)));
                    return {};
                }

                return {.Pass = pass, .Pin = pin, .Valid = true};
            }

            void ResolveEdge(const GraphEdgeEntry& edge)
            {
                const Endpoint source = ParseEndpoint(edge.From, edge.Location);
                const Endpoint destination = ParseEndpoint(edge.To, edge.Location);
                if (source.Valid && destination.Valid) Connect(source, destination, edge.Location);
            }

            void Connect(const Endpoint& source, const Endpoint& destination, const GraphSourceLocation& location)
            {
                if (source.Resource != kNone && destination.Resource != kNone)
                {
                    Error(location, "a connection has a pass on at least one side");
                    return;
                }

                if (source.Resource == kNone && !IsOutput(PinOf(source.Pass, source.Pin).Direction))
                {
                    Error(location, fmt::format("'{}' is an input; a connection goes from an output to an input", Describe(source)));
                    return;
                }

                // An output written into an application resource.
                if (destination.Resource != kNone)
                {
                    const PassRegistryData::Pin& pin = PinOf(source.Pass, source.Pin);
                    GraphFileData::Slot& slot = Out.Slots[SlotOf(source.Pass, source.Pin)];
                    if (pin.Direction != PinDirection::Output)
                    {
                        Error(location, fmt::format("'{}' changes the resource it receives; connect the application resource to its input side instead", Describe(source)));
                        return;
                    }
                    if (slot.TargetResource != kNone)
                    {
                        Error(location, fmt::format("'{}' already writes into '{}'", Describe(source), Out.Resources[slot.TargetResource]));
                        return;
                    }
                    slot.TargetResource = destination.Resource;
                    return;
                }

                const PassRegistryData::Pin& input = PinOf(destination.Pass, destination.Pin);
                const uint32_t inputSlot = SlotOf(destination.Pass, destination.Pin);
                if (!IsInput(input.Direction))
                {
                    Error(location, fmt::format("'{}' is an output; a connection goes from an output to an input", Describe(destination)));
                    return;
                }
                if (SlotEdges[inputSlot].Line != 0)
                {
                    Error(location, fmt::format("'{}' is already connected (line {}); an input has one source", Describe(destination), SlotEdges[inputSlot].Line));
                    return;
                }

                if (source.Resource != kNone)
                {
                    Out.Slots[inputSlot].SourceResource = source.Resource;
                }
                else
                {
                    const PassRegistryData::Pin& output = PinOf(source.Pass, source.Pin);
                    if (IsBufferUsage(output.Usage) != IsBufferUsage(input.Usage))
                    {
                        Error(location, fmt::format("'{}' is a {} and '{}' takes a {}", Describe(source), IsBufferUsage(output.Usage) ? "buffer" : "texture",
                                                    Describe(destination), IsBufferUsage(input.Usage) ? "buffer" : "texture"));
                        return;
                    }
                    if (source.Pass == destination.Pass)
                    {
                        Error(location, "a pass cannot read its own output");
                        return;
                    }

                    Out.Slots[inputSlot].SourceSlot = SlotOf(source.Pass, source.Pin);
                    Dependencies.emplace_back(source.Pass, destination.Pass);
                }

                SlotEdges[inputSlot] = location.Line != 0 ? location : GraphSourceLocation{.Line = 1};
            }

            void CheckConnections()
            {
                for (uint32_t passIndex = 0; passIndex < Out.Passes.size(); ++passIndex)
                {
                    const PassRegistryData::Type& type = TypeOf(passIndex);
                    for (uint32_t pin = 0; pin < type.PinCount; ++pin)
                    {
                        const PassRegistryData::Pin& description = PinOf(passIndex, pin);
                        const GraphFileData::Slot& slot = Out.Slots[SlotOf(passIndex, pin)];
                        const bool connected = slot.SourceSlot != kNone || slot.SourceResource != kNone;

                        if (IsInput(description.Direction) && !connected && !description.Optional)
                        {
                            Error(PassLocations[passIndex], fmt::format("'{}' is not connected", slot.DebugName));
                        }

                        if (description.Direction == PinDirection::Output && slot.TargetResource != kNone && (slot.HasFormatOverride || slot.ScaleOverride > 0.0f))
                        {
                            Error(PassLocations[passIndex], fmt::format("'{}' writes into '{}', so its format and size are that resource's", slot.DebugName, Out.Resources[slot.TargetResource]));
                        }

                        if (description.SizeOf != kNone && slot.TargetResource == kNone)
                        {
                            const GraphFileData::Slot& sizeSource = Out.Slots[SlotOf(passIndex, description.SizeOf)];
                            if (sizeSource.SourceSlot == kNone && sizeSource.SourceResource == kNone)
                            {
                                Error(PassLocations[passIndex], fmt::format("'{}' is sized like '{}', which is not connected", slot.DebugName, PinOf(passIndex, description.SizeOf).Name));
                            }
                        }
                    }
                }

                // Passes that write an application resource run before the passes that read it.
                for (uint32_t writer = 0; writer < Out.Passes.size(); ++writer)
                {
                    for (uint32_t pin = 0; pin < TypeOf(writer).PinCount; ++pin)
                    {
                        const uint32_t resource = Out.Slots[SlotOf(writer, pin)].TargetResource;
                        if (resource == kNone) continue;

                        for (uint32_t reader = 0; reader < Out.Passes.size(); ++reader)
                        {
                            if (reader == writer) continue;
                            for (uint32_t readerPin = 0; readerPin < TypeOf(reader).PinCount; ++readerPin)
                            {
                                if (Out.Slots[SlotOf(reader, readerPin)].SourceResource == resource) Dependencies.emplace_back(writer, reader);
                            }
                        }
                    }
                }
            }

            // The passes left unplaced are on a loop or after one: dropping, again and again, those that no other unplaced
            // pass depends on leaves the passes of the loop itself.
            void ReportLoop(std::vector<bool> placed)
            {
                const uint32_t passCount = static_cast<uint32_t>(Out.Passes.size());
                for (bool removed = true; removed;)
                {
                    removed = false;
                    for (uint32_t i = 0; i < passCount; ++i)
                    {
                        if (placed[i]) continue;
                        const bool feedsUnplaced = std::ranges::any_of(Dependencies, [&placed, i](const auto& dependency) { return dependency.first == i && !placed[dependency.second]; });
                        if (!feedsUnplaced)
                        {
                            placed[i] = true;
                            removed = true;
                        }
                    }
                }

                std::string loop;
                uint32_t first = kNone;
                for (uint32_t i = 0; i < passCount; ++i)
                {
                    if (placed[i]) continue;
                    if (first == kNone) first = i;
                    loop += fmt::format("{}'{}'", loop.empty() ? "" : ", ", Out.Passes[i].Name);
                }
                Error(PassLocations[first], fmt::format("these passes read each other's outputs in a loop: {}", loop));
            }

            // Every pass after the passes it depends on; among passes free to go, the one listed first in the file.
            void Order()
            {
                const uint32_t passCount = static_cast<uint32_t>(Out.Passes.size());
                std::vector<uint32_t> waitingFor(passCount, 0);
                for (const auto& [before, after] : Dependencies) ++waitingFor[after];

                std::vector<bool> placed(passCount, false);
                Out.Order.reserve(passCount);
                while (Out.Order.size() < passCount)
                {
                    uint32_t next = kNone;
                    for (uint32_t i = 0; i < passCount && next == kNone; ++i)
                    {
                        if (!placed[i] && waitingFor[i] == 0) next = i;
                    }

                    if (next == kNone)
                    {
                        ReportLoop(placed);
                        return;
                    }

                    placed[next] = true;
                    Out.Order.push_back(next);
                    for (const auto& [before, after] : Dependencies)
                    {
                        if (before == next) --waitingFor[after];
                    }
                }
            }
        };
    }

    bool GraphFileData::Resolve(const GraphFileDescription& description)
    {
        auto version = std::make_unique<Version>();
        Resolver resolver(Registry, description, *version);
        resolver.Resolve();

        if (!resolver.Errors.empty())
        {
            Logger->error("Render graph file {} has errors{}:\n{}", description.Path, Current ? "; the previous version stays in use" : "", fmt::join(resolver.Errors, "\n"));
            return false;
        }

        if (Current) Logger->info("Render graph file {} reloaded: {} passes", description.Path, version->Passes.size());
        Pending = std::move(version);
        return true;
    }

#if defined(EOS_GRAPH_TOOLS)
    bool GraphFileData::LoadFromDisk()
    {
        const ParsedGraphFile file = ParseGraphFile(Compiled.Path);
        if (!file.Errors.empty())
        {
            Logger->error("Render graph file {} has errors{}:\n{}", Compiled.Path, Current ? "; the previous version stays in use" : "", fmt::join(file.Errors, "\n"));
            return false;
        }
        return Resolve(file.Description());
    }
#endif

    // -------------------------------------------------------------------------------------------------------------------
    // Adding a file's passes to a frame

    void GraphFileData::AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources)
    {
        // A reload takes effect here, between frames: passes recorded earlier in this frame refer to the version in use.
        if (Pending)
        {
            Current = std::move(Pending);
            Reported.clear();
        }
        if (!Current) return;
        const Version& version = *Current;

        ApplicationResources.assign(version.Resources.size(), {});
        for (uint32_t i = 0; i < version.Resources.size(); ++i)
        {
            const auto given = std::ranges::find_if(resources, [name = std::string_view(version.Resources[i])](const GraphResource& resource) { return name == resource.Name; });
            if (given == resources.end())
            {
                ReportOnce(fmt::format("it connects to '{}', which the application does not provide; its passes do not run", version.Resources[i]));
                return;
            }
            ApplicationResources[i] = {.Texture = given->Texture, .Buffer = given->Buffer};
        }

        SlotResources.assign(version.Slots.size(), {});
        SkippedBecauseOf.assign(version.Passes.size(), kNone);

        // The application resource on a pin, or an invalid one (reported) when it is of the wrong kind.
        const auto applicationResource = [this, &version](uint32_t resource, const PassRegistryData::Pin& pin, const char* pinName) -> Resource
        {
            const Resource given = ApplicationResources[resource];
            if (IsBufferUsage(pin.Usage) ? given.Buffer.Valid() : given.Texture.Valid()) return given;

            ReportOnce(fmt::format("'{}' is connected to '{}', which the application gives as a {}", pinName, version.Resources[resource], given.Buffer.Valid() ? "buffer" : "texture"));
            return {};
        };

        for (const uint32_t passIndex : version.Order)
        {
            const Pass& pass = version.Passes[passIndex];
            const PassRegistryData::Type& type = Registry.Types[pass.Type];

            // Inputs, and the input side of input-outputs.
            bool inputsReady = true;
            uint32_t missingBecauseOf = kNone;
            for (uint32_t pin = 0; pin < type.PinCount; ++pin)
            {
                const PassRegistryData::Pin& description = Registry.Pins[type.FirstPin + pin];
                const uint32_t slotIndex = pass.FirstSlot + pin;
                const Slot& slot = version.Slots[slotIndex];
                if (!IsInput(description.Direction)) continue;

                Resource resource{};
                if (slot.SourceSlot != kNone) resource = SlotResources[slot.SourceSlot];
                else if (slot.SourceResource != kNone) resource = applicationResource(slot.SourceResource, description, slot.DebugName);

                const bool connected = slot.SourceSlot != kNone || slot.SourceResource != kNone;
                if (connected && !resource.Valid())
                {
                    inputsReady = false;
                    if (missingBecauseOf == kNone) missingBecauseOf = resource.DisabledBy;
                }
                SlotResources[slotIndex] = resource;
            }

            if (!pass.Enabled || !inputsReady)
            {
                // Disabled, or missing an input: readers get what the pass passes on (input-outputs and bypasses),
                // and outputs it would have created are missing because of the disabled pass behind it.
                const uint32_t cause = pass.Enabled ? missingBecauseOf : passIndex;
                if (pass.Enabled) SkippedBecauseOf[passIndex] = cause;

                for (uint32_t pin = 0; pin < type.PinCount; ++pin)
                {
                    const PassRegistryData::Pin& description = Registry.Pins[type.FirstPin + pin];
                    const Slot& slot = version.Slots[pass.FirstSlot + pin];
                    if (description.Direction != PinDirection::Output) continue;

                    Resource& resource = SlotResources[pass.FirstSlot + pin];
                    if (slot.TargetResource != kNone) resource = applicationResource(slot.TargetResource, description, slot.DebugName);
                    else if (description.BypassFrom != kNone) resource = SlotResources[pass.FirstSlot + description.BypassFrom];
                    if (!resource.Valid() && resource.DisabledBy == kNone) resource.DisabledBy = cause;
                }
                continue;
            }

            // Outputs.
            for (uint32_t pin = 0; pin < type.PinCount; ++pin)
            {
                const PassRegistryData::Pin& description = Registry.Pins[type.FirstPin + pin];
                const Slot& slot = version.Slots[pass.FirstSlot + pin];
                if (description.Direction != PinDirection::Output) continue;

                Resource& resource = SlotResources[pass.FirstSlot + pin];
                if (slot.TargetResource != kNone)
                {
                    resource = applicationResource(slot.TargetResource, description, slot.DebugName);
                    continue;
                }

                if (IsBufferUsage(description.Usage))
                {
                    resource.Buffer = graph.CreateBuffer({.Size = description.BufferSize, .DebugName = slot.DebugName});
                    continue;
                }

                GraphTextureDescription texture
                {
                    .TextureFormat = slot.HasFormatOverride ? slot.FormatOverride : description.TextureFormat,
                    .Scale = slot.ScaleOverride > 0.0f ? slot.ScaleOverride : description.Scale,
                    .DebugName = slot.DebugName,
                };
                if (texture.TextureFormat == Format::Invalid) texture.TextureFormat = graph.GetSwapchainFormat();
                if (description.LayerCount > 1)
                {
                    texture.Type = ImageType::Image_2D_Array;
                    texture.NumberOfLayers = description.LayerCount;
                }

                if (description.SizeOf != kNone)
                {
                    const Dimensions size = graph.GetSize(SlotResources[pass.FirstSlot + description.SizeOf].Texture);
                    texture.Size =
                    {
                        .Width = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(size.Width) * texture.Scale))),
                        .Height = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(size.Height) * texture.Scale))),
                        .Depth = 1,
                    };
                }

                resource.Texture = graph.CreateTexture(texture);
            }

            // Every pin becomes an access; targets are attached in pin order.
            PassBuilder builder = graph.AddPass(pass.Name, type.Kind);
            for (uint32_t pin = 0; pin < type.PinCount; ++pin)
            {
                const PassRegistryData::Pin& description = Registry.Pins[type.FirstPin + pin];
                const Resource resource = SlotResources[pass.FirstSlot + pin];
                if (!resource.Valid()) continue;

                const bool cleared = description.Direction == PinDirection::Output;
                const uint8_t layerCount = description.LayerCount;
                switch (description.Usage)
                {
                    case PinUsage::Sampled:
                        builder.Sample(resource.Texture);
                        break;
                    case PinUsage::Storage:
                        builder.Write(resource.Texture);
                        break;
                    case PinUsage::ColorTarget:
                        builder.Color({.Texture = resource.Texture, .Load = cleared ? LoadOp::Clear : LoadOp::Load, .ClearColor = description.ClearColor, .LayerCount = layerCount});
                        break;
                    case PinUsage::DepthTarget:
                        builder.Depth({.Texture = resource.Texture, .Load = cleared ? LoadOp::Clear : LoadOp::Load, .ClearDepth = description.ClearDepth, .LayerCount = layerCount});
                        break;
                    case PinUsage::DepthTest:
                        builder.Depth({.Texture = resource.Texture, .Load = LoadOp::Load, .LayerCount = layerCount, .ReadOnly = true});
                        break;
                    case PinUsage::ReadBuffer:
                        builder.Read(resource.Buffer);
                        break;
                    case PinUsage::WriteBuffer:
                        builder.Write(resource.Buffer);
                        break;
                    case PinUsage::IndirectBuffer:
                        builder.Indirect(resource.Buffer);
                        break;
                    case PinUsage::TransferBuffer:
                        builder.CopyTo(resource.Buffer);
                        break;
                }
            }

            builder.Execute([this, passIndex](PassContext& context)
            {
                Registry.Types[Current->Passes[passIndex].Type].Execute(context, PassData(*this, passIndex));
            });
        }

        // A disabled pass without bypasses takes the passes that need its outputs down with it: say so once.
        for (uint32_t disabled = 0; disabled < version.Passes.size(); ++disabled)
        {
            std::string skipped;
            for (const uint32_t passIndex : version.Order)
            {
                if (SkippedBecauseOf[passIndex] == disabled) skipped += fmt::format("{}'{}'", skipped.empty() ? "" : ", ", version.Passes[passIndex].Name);
            }
            if (!skipped.empty())
            {
                ReportOnce(fmt::format("'{}' is disabled and passes nothing on for some of its outputs, so these passes do not run either: {}", version.Passes[disabled].Name, skipped), false);
            }
        }
    }

    uint32_t GraphFileData::FindSlot(std::string_view pin) const
    {
        if (!Current) return kNone;

        const size_t dot = pin.find('.');
        if (dot == std::string_view::npos) return kNone;

        const uint32_t pass = Current->FindPass(pin.substr(0, dot));
        if (pass == kNone) return kNone;

        const uint32_t index = Registry.FindPin(Registry.Types[Current->Passes[pass].Type], pin.substr(dot + 1));
        return index == kNone ? kNone : Current->Passes[pass].FirstSlot + index;
    }

    // -------------------------------------------------------------------------------------------------------------------
    // PassData

    const char* PassData::Name() const
    {
        return File.Current->Passes[Pass].Name;
    }

    uint32_t PassData::FindPin(std::string_view pin) const
    {
        const GraphFileData::Pass& pass = File.Current->Passes[Pass];
        const PassRegistryData::Type& type = File.Registry.Types[pass.Type];
        const uint32_t index = File.Registry.FindPin(type, pin);
        CHECK(index != kNone, "Pass type '{}' has no pin '{}'", type.Name, pin);
        return index == kNone ? kNone : pass.FirstSlot + index;
    }

    const PropertyValue& PassData::FindProperty(std::string_view property, PropertyType type) const
    {
        static constexpr PropertyValue kMissing{};

        const GraphFileData::Pass& pass = File.Current->Passes[Pass];
        const PassRegistryData::Type& passType = File.Registry.Types[pass.Type];
        const uint32_t index = File.Registry.FindProperty(passType, property);
        CHECK(index != kNone, "Pass type '{}' has no property '{}'", passType.Name, property);
        if (index == kNone) return kMissing;

        const PropertyType actual = File.Registry.Properties[passType.FirstProperty + index].Type;
        const bool compatible = actual == type || (type == PropertyType::Int && actual == PropertyType::Choice);
        CHECK(compatible, "Property '{}' of pass type '{}' is {}, not {}", property, passType.Name, ToString(actual), ToString(type));
        return compatible ? File.Current->Values[pass.FirstValue + index] : kMissing;
    }

    GraphTexture PassData::Texture(std::string_view pin) const
    {
        const uint32_t slot = FindPin(pin);
        return slot == kNone ? GraphTexture{} : File.SlotResources[slot].Texture;
    }

    GraphBuffer PassData::Buffer(std::string_view pin) const
    {
        const uint32_t slot = FindPin(pin);
        return slot == kNone ? GraphBuffer{} : File.SlotResources[slot].Buffer;
    }

    bool PassData::Bool(std::string_view property) const { return FindProperty(property, PropertyType::Bool).Bool; }
    int32_t PassData::Int(std::string_view property) const { return FindProperty(property, PropertyType::Int).Int; }
    float PassData::Float(std::string_view property) const { return FindProperty(property, PropertyType::Float).Float.x; }
    glm::vec2 PassData::Float2(std::string_view property) const { return glm::vec2(FindProperty(property, PropertyType::Float2).Float); }
    glm::vec3 PassData::Float3(std::string_view property) const { return glm::vec3(FindProperty(property, PropertyType::Float3).Float); }
    glm::vec4 PassData::Float4(std::string_view property) const { return FindProperty(property, PropertyType::Float4).Float; }

    // -------------------------------------------------------------------------------------------------------------------
    // GraphFile

    GraphFile::GraphFile(const PassRegistry& registry, const GraphFileDescription& file)
    : File(std::make_unique<GraphFileData>(*registry.Types, file))
    {
#if defined(EOS_GRAPH_TOOLS)
        // The YAML is what is being edited, so it wins; the build's version is the fallback when it has errors.
        std::error_code error;
        File->LastWriteTime = std::filesystem::last_write_time(file.Path, error);
        if (!File->LoadFromDisk())
        {
            Logger->warn("Render graph file {}: using the version built into the application", file.Path);
            File->Resolve(file);
        }
#else
        File->Resolve(file);
#endif
        if (File->Pending) File->Current = std::move(File->Pending);
    }

    GraphFile::~GraphFile() = default;

    bool GraphFile::Reload()
    {
#if defined(EOS_GRAPH_TOOLS)
        // An editor that saves by replacing the file leaves a moment where it does not exist; the next call sees it.
        std::error_code error;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(File->Compiled.Path, error);
        if (error || writeTime == File->LastWriteTime) return false;

        File->LastWriteTime = writeTime;
        return File->LoadFromDisk();
#else
        return false;
#endif
    }

    void GraphFile::AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources)
    {
        File->AddTo(graph, resources);
    }

    bool GraphFile::IsLoaded() const
    {
        return File->Current != nullptr || File->Pending != nullptr;
    }

    GraphTexture GraphFile::GetTexture(std::string_view pin) const
    {
        const uint32_t slot = File->FindSlot(pin);
        return slot < File->SlotResources.size() ? File->SlotResources[slot].Texture : GraphTexture{};
    }

    GraphBuffer GraphFile::GetBuffer(std::string_view pin) const
    {
        const uint32_t slot = File->FindSlot(pin);
        return slot < File->SlotResources.size() ? File->SlotResources[slot].Buffer : GraphBuffer{};
    }

    bool GraphFile::SetEnabled(std::string_view pass, bool enabled)
    {
        const uint32_t index = File->Current ? File->Current->FindPass(pass) : kNone;
        if (index == kNone) return false;

        File->Current->Passes[index].Enabled = enabled;
        return true;
    }

    namespace
    {
        [[nodiscard]] PropertyValue* FindValue(GraphFileData& file, std::string_view passName, std::string_view propertyName, PropertyType type)
        {
            const uint32_t passIndex = file.Current ? file.Current->FindPass(passName) : kNone;
            if (passIndex == kNone) return nullptr;

            const GraphFileData::Pass& pass = file.Current->Passes[passIndex];
            const PassRegistryData::Type& passType = file.Registry.Types[pass.Type];
            const uint32_t property = file.Registry.FindProperty(passType, propertyName);
            if (property == kNone) return nullptr;

            const PropertyType actual = file.Registry.Properties[passType.FirstProperty + property].Type;
            if (actual != type && !(type == PropertyType::Int && actual == PropertyType::Choice)) return nullptr;
            return &file.Current->Values[pass.FirstValue + property];
        }
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, bool value)
    {
        PropertyValue* stored = FindValue(*File, pass, property, PropertyType::Bool);
        if (stored) stored->Bool = value;
        return stored != nullptr;
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, int32_t value)
    {
        PropertyValue* stored = FindValue(*File, pass, property, PropertyType::Int);
        if (stored) stored->Int = value;
        return stored != nullptr;
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, float value)
    {
        PropertyValue* stored = FindValue(*File, pass, property, PropertyType::Float);
        if (stored) stored->Float.x = value;
        return stored != nullptr;
    }

    uint32_t GraphFile::GetPassCount() const
    {
        return File->Current ? static_cast<uint32_t>(File->Current->Passes.size()) : 0;
    }

    GraphFilePass GraphFile::GetPass(uint32_t pass)
    {
        CHECK(pass < GetPassCount(), "No pass {} in the graph file", pass);
        GraphFileData::Pass& entry = File->Current->Passes[pass];
        const PassRegistryData::Type& type = File->Registry.Types[entry.Type];
        return {.Name = entry.Name, .Type = type.Name, .Enabled = &entry.Enabled, .PropertyCount = type.PropertyCount};
    }

    GraphFileProperty GraphFile::GetProperty(uint32_t pass, uint32_t property)
    {
        CHECK(pass < GetPassCount(), "No pass {} in the graph file", pass);
        const GraphFileData::Pass& entry = File->Current->Passes[pass];
        const PassRegistryData::Type& type = File->Registry.Types[entry.Type];
        CHECK(property < type.PropertyCount, "Pass '{}' has no property {}", entry.Name, property);

        const PassRegistryData::Property& description = File->Registry.Properties[type.FirstProperty + property];
        return
        {
            .Name = description.Name,
            .Type = description.Type,
            .Value = &File->Current->Values[entry.FirstValue + property],
            .Min = description.Min,
            .Max = description.Max,
            .Choices = File->Registry.GetChoices(description),
        };
    }
}
