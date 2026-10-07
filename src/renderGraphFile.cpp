#include "renderGraphFile.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <ranges>
#include <set>
#include <string>
#include <system_error>

#include "formatNames.h"
#include "graphFileParser.h"
#include "logger.h"

namespace EOS
{
    namespace
    {
        constexpr uint32_t kNone = 0xFFFFFFFF;

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
                case PropertyType::String: return "a string";
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
            uint32_t Width = 0;                     // a fixed size; 0 when it follows the window or SizeOf
            uint32_t Height = 0;
            const char* BufferType = "";
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
            PassSetupFunction Setup;
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

        // A pass written in Slang (eos.pass), with what recording it needs.
        struct ShaderPass final
        {
            std::string Module;
            uint32_t Type = kNone;                  // kNone while its current version cannot be registered
            ShaderProgramHolder Program;
            std::shared_ptr<const CompiledShaderProgram> Reflection;    // the version its type was registered from
            bool Compute = false;
            bool ReportedMissingScene = false;
            bool OwnVertexShader = false;
            bool GeometryShader = false;
            ComputePipelineHolder ComputePipeline;

            // Raster passes: one pipeline per fragment entry point and set of target formats it has been used with.
            struct RasterPipeline final
            {
                std::array<Format, EOS_MAX_COLOR_ATTACHMENTS> Formats{};
                Format DepthFormat = Format::Invalid;
                uint32_t Fragment = 0;              // into ShaderPassReflection::Fragments
                RenderPipelineHolder Pipeline;
            };
            std::vector<RasterPipeline> RasterPipelines;
        };

        // A sampler of a [Sampler] description, shared by every Slang pass that asks for it.
        struct CachedSampler final
        {
            uint8_t Filter = 1;
            uint8_t Address = 0;
            SamplerHolder Sampler;
        };

        IContext* Context = nullptr;
        std::vector<std::unique_ptr<ShaderPass>> ShaderPasses;     // in place, for the pass functions that refer to them
        std::vector<CachedSampler> Samplers;
        ShaderProgramHolder FullscreenProgram;      // eos.fullscreen's vertex shader, for raster passes without one
        uint64_t Generation = 0;                    // changes when a Slang pass changes its pins or properties

        // Registers description; returns why not otherwise.
        [[nodiscard]] std::string Add(PassTypeDescription& description, uint32_t& outType);

        // A registered type, or the Slang pass of that module name, loaded now.
        [[nodiscard]] uint32_t FindOrLoadType(std::string_view name);
        [[nodiscard]] uint32_t LoadShaderPass(std::string_view module);
        [[nodiscard]] uint32_t RegisterShaderPass(ShaderPass& pass, const CompiledShaderProgram& program);

        // Registers again the Slang passes whose pins or properties a hot reload changed.
        void Refresh();

        void ExecuteShaderPass(ShaderPass& pass, PassContext& context, const PassData& data);
        [[nodiscard]] RenderPipelineHandle GetRasterPipeline(ShaderPass& pass, const std::array<Format, EOS_MAX_COLOR_ATTACHMENTS>& formats, uint32_t targetCount, Format depthFormat, uint32_t fragment);
        [[nodiscard]] SamplerHandle GetSampler(uint8_t filter, uint8_t address);

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
            const SceneDrawData* Scene = nullptr;   // a scene buffer: how to draw it
            const void* Host = nullptr;             // what a C++ pass type uploaded into the buffer this frame
            size_t HostSize = 0;
            uint32_t DisabledBy = kNone;            // when missing: the disabled pass that would have written it

            [[nodiscard]] bool Valid() const { return Texture.Valid() || Buffer.Valid(); }
        };

        GraphFileData(PassRegistryData& registry, std::filesystem::path path) : Registry(registry), Path(std::move(path)) {}

        PassRegistryData& Registry;
        std::filesystem::path Path;
        uint64_t ResolvedGeneration = 0;            // of the registry, when the version in use was resolved
        std::filesystem::file_time_type LastWriteTime{};
        bool LastLoadFailed = false;                // the file read last had errors, maybe in a pass it uses
        std::unique_ptr<ParsedGraphFile> Parsed;    // the YAML of the version in use, to resolve again
        std::unique_ptr<Version> Current;
        std::unique_ptr<Version> Pending;           // loaded by Reload(); replaces Current at the next AddTo

        // This frame.
        std::vector<Resource> SlotResources;
        std::vector<std::unique_ptr<std::byte[]>> HostCopies;   // of PassSetup::Upload
        std::vector<Resource> ApplicationResources;
        std::vector<uint32_t> SkippedBecauseOf;     // per pass: the disabled pass it needs output from

        // Problems found while adding passes are reported once per version of the file, not every frame.
        std::set<std::string> Reported;

        void ReportOnce(std::string message, bool error = true)
        {
            if (!Reported.insert(message).second) return;
            if (error) Logger->error("Render graph file {}: {}", Path.generic_string(), message);
            else Logger->warn("Render graph file {}: {}", Path.generic_string(), message);
        }

        // Puts description, resolved against the pass types, into Pending. Returns false (and reports why) on errors.
        bool Resolve(const GraphFileDescription& description);

        // Parses the YAML file, then resolves it.
        bool LoadFromDisk();
        void AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources);
        [[nodiscard]] uint32_t FindSlot(std::string_view pin) const;
    };

    // -------------------------------------------------------------------------------------------------------------------
    // PassRegistry

    PassRegistry::PassRegistry(IContext* context)
    : Types(std::make_unique<PassRegistryData>())
    {
        Types->Context = context;
    }

    PassRegistry::~PassRegistry() = default;

    std::string PassRegistryData::Add(PassTypeDescription& description, uint32_t& outType)
    {
        PassRegistryData& registry = *this;
        outType = kNone;
        const std::string_view name = description.Name;

        if (!(!name.empty() && name.find('.') == std::string_view::npos)) return fmt::format("Pass type '{}': names are not empty and have no '.'", name);
        if (!(registry.FindType(name) == kNone)) return fmt::format("Pass type '{}' is registered twice", name);
        if (!(static_cast<bool>(description.Execute) || static_cast<bool>(description.Setup))) return fmt::format("Pass type '{}' has neither an Execute nor a Setup function", name);

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

            if (!(!pinName.empty() && pinName.find('.') == std::string_view::npos)) return fmt::format("Pass type '{}': pin names are not empty and have no '.'", name);
            if (!(findPin(pinName) == i)) return fmt::format("Pass type '{}' has two pins named '{}'", name, pinName);
            if (!(std::ranges::none_of(description.Properties, [pinName](const PropertyDescription& property) { return pinName == property.Name; }))) return fmt::format("Pass type '{}': '{}' is both a pin and a property", name, pinName);
            // A type with only Setup records nothing, so how its pins would be used in a pass does not matter.
            const bool records = static_cast<bool>(description.Execute);
            if (!(!records || !target || description.Kind == PassKind::Raster)) return fmt::format("Pass type '{}': only raster passes have targets (pin '{}')", name, pinName);
            if (!(!records || description.Kind != PassKind::Transfer || pin.Usage == PinUsage::TransferBuffer)) return fmt::format("Pass type '{}': transfer passes only have transfer buffers (pin '{}')", name, pinName);

            switch (pin.Usage)
            {
                case PinUsage::Sampled:
                case PinUsage::DepthTest:
                case PinUsage::ReadBuffer:
                case PinUsage::IndirectBuffer:
                    if (!(pin.Direction == PinDirection::Input)) return fmt::format("Pass type '{}': pin '{}' only reads, so it is an input", name, pinName);
                    break;
                default:
                    break;
            }

            if (pin.Direction == PinDirection::Output)
            {
                if (texture)
                {
                    if (!(pin.Usage != PinUsage::DepthTarget || IsDepthFormat(pin.TextureFormat))) return fmt::format("Pass type '{}': depth output '{}' needs a depth format", name, pinName);
                    if (!(pin.Usage == PinUsage::DepthTarget || !IsDepthFormat(pin.TextureFormat))) return fmt::format("Pass type '{}': output '{}' is not a depth target but has a depth format", name, pinName);
                    if (!(pin.Output.Scale > 0.0f)) return fmt::format("Pass type '{}': output '{}' needs a positive scale", name, pinName);
                    if (!((pin.Output.Width == 0) == (pin.Output.Height == 0))) return fmt::format("Pass type '{}': output '{}' has a fixed width and height or neither", name, pinName);
                }
                else
                {
                    // Setup can hand out buffers of its own instead.
                    if (!(pin.BufferSize > 0 || description.Setup)) return fmt::format("Pass type '{}': buffer output '{}' needs a size", name, pinName);
                }

                const std::string_view sizeOf = pin.Output.SizeOf;
                if (!sizeOf.empty())
                {
                    const uint32_t source = findPin(sizeOf);
                    if (!(texture && source != kNone && IsInput(description.Pins[source].Direction) && !IsBufferUsage(description.Pins[source].Usage))) return fmt::format("Pass type '{}': output '{}' is sized like '{}', which is not a texture input", name, pinName, sizeOf);
                }

                const std::string_view bypass = pin.Output.BypassFrom;
                if (!bypass.empty())
                {
                    const uint32_t source = findPin(bypass);
                    if (!(source != kNone && IsInput(description.Pins[source].Direction) && IsBufferUsage(description.Pins[source].Usage) == !texture)) return fmt::format("Pass type '{}': output '{}' bypasses to '{}', which is not an input of the same kind", name, pinName, bypass);
                }
            }
        }

        for (uint32_t i = 0; i < description.Properties.size(); ++i)
        {
            const PropertyDescription& property = description.Properties[i];
            const std::string_view propertyName = property.Name;

            if (!(IsIdentifier(propertyName) && propertyName != "type" && propertyName != "enabled")) return fmt::format("Pass type '{}': property '{}' needs a name of letters, digits and '_' other than 'type' and 'enabled'", name, propertyName);
            if (!(std::ranges::count_if(description.Properties, [propertyName](const PropertyDescription& other) { return propertyName == other.Name; }) == 1)) return fmt::format("Pass type '{}' has two properties named '{}'", name, propertyName);
            if (!(property.Type != PropertyType::Choice || (!property.Choices.empty() && property.Default.Int >= 0 && property.Default.Int < static_cast<int32_t>(property.Choices.size())))) return fmt::format("Pass type '{}': choice '{}' needs choices and a default among them", name, propertyName);
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
            .Setup = std::move(description.Setup),
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
                .Width = pin.Output.Width,
                .Height = pin.Output.Height,
                .BufferType = registry.Intern(pin.BufferType),
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

        outType = static_cast<uint32_t>(registry.Types.size());
        registry.Types.push_back(std::move(type));
        return {};
    }

    void PassRegistry::Register(PassTypeDescription description)
    {
        uint32_t type = kNone;
        const std::string error = Types->Add(description, type);
        CHECK(error.empty(), "{}", error);
    }

    // -------------------------------------------------------------------------------------------------------------------
    // Passes written in Slang

    uint32_t PassRegistryData::FindOrLoadType(std::string_view name)
    {
        if (const uint32_t type = FindType(name); type != kNone) return type;

        // A Slang pass whose current version could not be registered was reported when it changed; do not load it twice.
        const auto known = std::ranges::find_if(ShaderPasses, [name](const std::unique_ptr<ShaderPass>& pass) { return pass->Module == name; });
        if (known != ShaderPasses.end()) return (*known)->Type;

        return LoadShaderPass(name);
    }

    uint32_t PassRegistryData::LoadShaderPass(std::string_view module)
    {
        if (!Context || !IsIdentifier(module)) return kNone;

        ShaderProgramHolder program = Context->CreateShaderProgram({.Module = std::string(module)});
        if (program.Empty()) return kNone;      // CreateShaderProgram reported why

        const std::shared_ptr<const CompiledShaderProgram> reflection = Context->GetShaderProgram(program);
        if (!reflection || !reflection->Pass.IsPass)
        {
            Logger->error("Shader module '{}' is not a render graph pass: its push constants are not a struct marked [Pass] (eos.pass)", module);
            return kNone;
        }

        auto pass = std::make_unique<ShaderPass>();
        pass->Module = module;
        pass->Program = std::move(program);
        pass->Reflection = reflection;
        pass->Type = RegisterShaderPass(*pass, *reflection);
        if (pass->Type == kNone) return kNone;

        if (pass->Compute)
        {
            pass->ComputePipeline = Context->CreateComputePipeline({.ComputeShader = {pass->Program}, .DebugName = pass->Module.c_str()});
        }

        const uint32_t type = pass->Type;
        ShaderPasses.push_back(std::move(pass));
        return type;
    }

    // Turns the pass a Slang module declares into a pass type: its fields into pins and properties, and recording into
    // ExecuteShaderPass.
    uint32_t PassRegistryData::RegisterShaderPass(ShaderPass& pass, const CompiledShaderProgram& program)
    {
        const auto hasStage = [&program](ShaderStage stage) { return std::ranges::any_of(program.EntryPoints, [stage](const ShaderEntryPoint& entryPoint) { return entryPoint.Stage == stage; }); };
        pass.Compute = hasStage(ShaderStage::Compute);
        pass.OwnVertexShader = hasStage(ShaderStage::Vertex);
        pass.GeometryShader = hasStage(ShaderStage::Geometry);

        PassTypeDescription description
        {
            .Name = pass.Module.c_str(),
            .Kind = pass.Compute ? PassKind::Compute : PassKind::Raster,
            .Execute = [this, &pass](PassContext& context, const PassData& data) { ExecuteShaderPass(pass, context, data); },
        };

        const auto formatOf = [](const ShaderPassField& field) { return field.Format.empty() ? EOS::Pin::SwapchainFormat : FindFormat(field.Format)->Value; };
        const auto outputOf = [](const ShaderPassField& field) -> OutputOptions
        {
            return
            {
                .Scale = field.Scale,
                .SizeOf = field.SizeOf.c_str(),
                .Width = field.Width,
                .Height = field.Height,
                .ClearColor = field.Clear,
                .LayerCount = static_cast<uint8_t>(field.Layers),
                .BypassFrom = field.Bypass.c_str(),
            };
        };
        const auto optional = [](const ShaderPassField& field, PinDescription pin) { return field.Optional ? EOS::Pin::Optional(pin) : pin; };
        std::vector<std::vector<const char*>> choices;
        choices.reserve(program.Pass.Fields.size());

        // Color targets are attached in pin order, so they go in by location.
        std::vector<const ShaderPassField*> targets;
        for (const ShaderPassField& field : program.Pass.Fields)
        {
            if (field.Kind == ShaderPassFieldKind::ColorTarget) targets.push_back(&field);
        }
        std::ranges::sort(targets, {}, &ShaderPassField::Offset);
        for (const ShaderPassField* target : targets)
        {
            description.Pins.push_back(target->Direction == ShaderPassDirection::InOut
                                       ? EOS::Pin::ColorInOut(target->Name.c_str())
                                       : EOS::Pin::ColorOutput(target->Name.c_str(), formatOf(*target), outputOf(*target)));
        }

        for (const ShaderPassField& field : program.Pass.Fields)
        {
            const char* name = field.Name.c_str();
            const float defaultValue = static_cast<float>(field.Default);

            switch (field.Kind)
            {
                case ShaderPassFieldKind::Texture:
                    // A texture marked [DepthTest] is also the read-only depth target.
                    description.Pins.push_back(optional(field, field.DepthCompare >= 0 ? EOS::Pin::DepthTest(name) : EOS::Pin::Sampled(name)));
                    break;
                case ShaderPassFieldKind::StorageTexture:
                    description.Pins.push_back(field.Direction == ShaderPassDirection::InOut ? EOS::Pin::StorageInOut(name) : EOS::Pin::StorageOutput(name, formatOf(field), outputOf(field)));
                    break;
                case ShaderPassFieldKind::Buffer:
                case ShaderPassFieldKind::Scene:
                {
                    const char* type = field.BufferType.c_str();
                    if (field.Direction == ShaderPassDirection::Input) description.Pins.push_back(optional(field, EOS::Pin::Typed(EOS::Pin::ReadBuffer(name), type)));
                    else if (field.Direction == ShaderPassDirection::InOut) description.Pins.push_back(EOS::Pin::Typed(EOS::Pin::BufferInOut(name), type));
                    else description.Pins.push_back(EOS::Pin::Typed(EOS::Pin::BufferOutput(name, field.BufferSize, {.BypassFrom = field.Bypass.c_str()}), type));
                    break;
                }
                case ShaderPassFieldKind::DepthTarget:
                    if (field.Direction == ShaderPassDirection::Input) description.Pins.push_back(optional(field, EOS::Pin::DepthTest(name)));
                    else if (field.Direction == ShaderPassDirection::InOut) description.Pins.push_back(EOS::Pin::DepthInOut(name));
                    else description.Pins.push_back(EOS::Pin::DepthOutput(name, formatOf(field), outputOf(field)));
                    break;
                case ShaderPassFieldKind::ColorTarget:
                    break;
                case ShaderPassFieldKind::Sampler:
                    // Created now rather than when the pass is recorded: a descriptor created while a frame records
                    // is not in the bindless set that frame uses.
                    static_cast<void>(GetSampler(field.SamplerFilter, field.SamplerAddress));
                    break;
                case ShaderPassFieldKind::Bool:
                    description.Properties.push_back(EOS::Property::Bool(name, field.Default != 0.0));
                    break;
                case ShaderPassFieldKind::Int:
                case ShaderPassFieldKind::UInt:
                    description.Properties.push_back(EOS::Property::Int(name, static_cast<int32_t>(field.Default), static_cast<int32_t>(field.Minimum), static_cast<int32_t>(field.Maximum)));
                    break;
                case ShaderPassFieldKind::Float:
                    description.Properties.push_back(EOS::Property::Float(name, defaultValue, field.Minimum, field.Maximum));
                    break;
                case ShaderPassFieldKind::Float2:
                    description.Properties.push_back(EOS::Property::Float2(name, glm::vec2(0.0f), field.Minimum, field.Maximum));
                    break;
                case ShaderPassFieldKind::Float3:
                    description.Properties.push_back(EOS::Property::Float3(name, glm::vec3(0.0f), field.Minimum, field.Maximum));
                    break;
                case ShaderPassFieldKind::Float4:
                    description.Properties.push_back(EOS::Property::Float4(name, glm::vec4(0.0f), field.Minimum, field.Maximum));
                    break;
                case ShaderPassFieldKind::Enum:
                {
                    std::vector<const char*>& names = choices.emplace_back();
                    for (const std::string& enumName : field.EnumNames) names.push_back(enumName.c_str());
                    const auto defaultCase = std::ranges::find(field.EnumValues, static_cast<int64_t>(field.Default));
                    const int32_t defaultIndex = defaultCase == field.EnumValues.end() ? 0 : static_cast<int32_t>(defaultCase - field.EnumValues.begin());
                    description.Properties.push_back(EOS::Property::Choice(name, names, defaultIndex));
                    break;
                }
            }
        }

        uint32_t type = kNone;
        const std::string error = Add(description, type);
        if (!error.empty()) Logger->error("Render graph pass '{}' cannot be used: {}", pass.Module, error);
        return type;
    }

    void PassRegistryData::Refresh()
    {
        if (!Context) return;

        for (const std::unique_ptr<ShaderPass>& pass : ShaderPasses)
        {
            const std::shared_ptr<const CompiledShaderProgram> current = Context->GetShaderProgram(pass->Program);
            if (!current || current == pass->Reflection) continue;

            // Recompiled with the same pins and properties: its pipelines were rebuilt, the type stays as it is.
            const bool samePass = current->Pass == pass->Reflection->Pass;
            pass->Reflection = current;
            if (samePass) continue;

            // Otherwise the type is registered again under its name, and graph files resolve against the new one.
            if (pass->Type != kNone) Types[pass->Type].Name = "";
            pass->RasterPipelines.clear();
            pass->Type = current->Pass.IsPass ? RegisterShaderPass(*pass, *current) : kNone;
            if (!current->Pass.IsPass) Logger->error("Shader module '{}' is no longer a render graph pass: its push constants are not a struct marked [Pass]", pass->Module);
            else if (pass->Type != kNone) Logger->info("Render graph pass '{}' changed its pins or properties; the graph files using it are resolved again", pass->Module);
            ++Generation;
        }
    }

    RenderPipelineHandle PassRegistryData::GetRasterPipeline(ShaderPass& pass, const std::array<Format, EOS_MAX_COLOR_ATTACHMENTS>& formats, uint32_t targetCount, Format depthFormat, uint32_t fragment)
    {
        for (const ShaderPass::RasterPipeline& pipeline : pass.RasterPipelines)
        {
            if (pipeline.Formats == formats && pipeline.DepthFormat == depthFormat && pipeline.Fragment == fragment) return pipeline.Pipeline;
        }

        const ShaderPassReflection& reflection = pass.Reflection->Pass;
        RenderPipelineDescription description{};
        if (pass.OwnVertexShader)
        {
            description.VertexShader = {pass.Program};
        }
        else
        {
            if (FullscreenProgram.Empty()) FullscreenProgram = Context->CreateShaderProgram({.Module = "eos.fullscreen"});
            description.VertexShader = {FullscreenProgram};
        }
        if (pass.GeometryShader) description.GeometryShader = {pass.Program};
        description.FragmentShader = {pass.Program, reflection.Fragments[fragment].EntryPoint.c_str()};
        for (uint32_t i = 0; i < targetCount; ++i) description.ColorAttachments[i].ColorFormat = formats[i];
        description.DepthFormat = depthFormat;
        description.PipelineCullMode = static_cast<CullMode>(reflection.Cull);
        description.DepthClamping = reflection.DepthClamp;
        description.DebugName = pass.Module.c_str();

        ShaderPass::RasterPipeline& pipeline = pass.RasterPipelines.emplace_back();
        pipeline.Formats = formats;
        pipeline.DepthFormat = depthFormat;
        pipeline.Fragment = fragment;
        pipeline.Pipeline = Context->CreateRenderPipeline(description);
        return pipeline.Pipeline;
    }

    SamplerHandle PassRegistryData::GetSampler(uint8_t filter, uint8_t address)
    {
        for (const CachedSampler& sampler : Samplers)
        {
            if (sampler.Filter == filter && sampler.Address == address) return sampler.Sampler;
        }

        const bool linear = filter != 0;
        const SamplerWrap wrap = static_cast<SamplerWrap>(address);
        CachedSampler& sampler = Samplers.emplace_back(CachedSampler{.Filter = filter, .Address = address});
        sampler.Sampler = Context->CreateSampler(
        {
            .minFilter = linear ? LinearFilter : NearestFilter,
            .magFilter = linear ? LinearFilter : NearestFilter,
            .mipMap = linear ? SamplerMip::Linear : SamplerMip::Nearest,
            .wrapU = wrap,
            .wrapV = wrap,
            .wrapW = wrap,
            .mipLodMax = EOS_MAX_MIP_LEVELS,
            .maxAnisotropic = 0,
            .debugName = "Render Graph Sampler",
        });
        return sampler.Sampler;
    }

    // Fills the push constants from the pass's pins and properties, by the offsets of its reflection, and records it: a
    // dispatch over its first storage texture output, or draws into its targets (a fullscreen triangle, 3 vertices of
    // its own vertex shader, or the scene).
    void PassRegistryData::ExecuteShaderPass(ShaderPass& pass, PassContext& context, const PassData& data)
    {
        const CompiledShaderProgram& program = *pass.Reflection;
        const ShaderPassReflection& reflection = program.Pass;
        std::array<std::byte, 256> constants{};
        CHECK_RETURN(program.PushConstantSize <= constants.size(), "Render graph pass '{}': {} bytes of push constants", pass.Module, program.PushConstantSize);

        const auto write = [&constants](uint32_t offset, const void* value, size_t size)
        {
            if (offset + size <= constants.size()) std::memcpy(constants.data() + offset, value, size);
        };

        Dimensions dispatchSize{0, 0, 0};
        std::array<Format, EOS_MAX_COLOR_ATTACHMENTS> targetFormats{};
        uint32_t targetCount = 0;
        Format depthFormat = Format::Invalid;
        DepthState depthState{};
        const SceneDrawData* scene = nullptr;

        for (const ShaderPassField& field : reflection.Fields)
        {
            switch (field.Kind)
            {
                case ShaderPassFieldKind::Texture:
                case ShaderPassFieldKind::StorageTexture:
                {
                    const GraphTexture texture = data.Texture(field.Name);
                    const DescriptorHandle handle = texture.Valid() ? context.Descriptor(texture) : DescriptorHandle{};
                    write(field.Offset, &handle, sizeof(handle));
                    if (field.Kind == ShaderPassFieldKind::StorageTexture && field.Direction != ShaderPassDirection::Input && dispatchSize.Width == 0 && texture.Valid())
                    {
                        dispatchSize = context.Size(texture);
                    }
                    if (field.DepthCompare >= 0 && texture.Valid())
                    {
                        depthFormat = Context->GetFormat(context.Texture(texture));
                        depthState = {.CompareOpState = static_cast<CompareOp>(field.DepthCompare), .IsDepthWriteEnabled = false};
                    }
                    break;
                }
                case ShaderPassFieldKind::Buffer:
                case ShaderPassFieldKind::Scene:
                {
                    const GraphBuffer buffer = data.Buffer(field.Name);
                    const uint64_t address = buffer.Valid() ? context.Address(buffer) : 0;
                    write(field.Offset, &address, sizeof(address));
                    if (field.Kind == ShaderPassFieldKind::Scene) scene = data.Scene(field.Name);
                    break;
                }
                case ShaderPassFieldKind::DepthTarget:
                {
                    const GraphTexture texture = data.Texture(field.Name);
                    if (!texture.Valid()) break;

                    // Outputs and input-outputs write depth; an input is only tested against.
                    const bool input = field.Direction == ShaderPassDirection::Input;
                    const CompareOp compare = field.DepthCompare >= 0 ? static_cast<CompareOp>(field.DepthCompare) : input ? CompareOp::LessEqual : CompareOp::Less;
                    depthFormat = Context->GetFormat(context.Texture(texture));
                    depthState = {.CompareOpState = compare, .IsDepthWriteEnabled = !input};
                    break;
                }
                case ShaderPassFieldKind::ColorTarget:
                {
                    const GraphTexture texture = data.Texture(field.Name);
                    if (texture.Valid() && field.Offset < targetFormats.size())
                    {
                        targetFormats[field.Offset] = Context->GetFormat(context.Texture(texture));
                        targetCount = std::max(targetCount, field.Offset + 1);
                    }
                    break;
                }
                case ShaderPassFieldKind::Sampler:
                {
                    const DescriptorHandle handle(GetSampler(field.SamplerFilter, field.SamplerAddress));
                    write(field.Offset, &handle, sizeof(handle));
                    break;
                }
                case ShaderPassFieldKind::Bool:
                {
                    const uint32_t value = data.Bool(field.Name) ? 1u : 0u;
                    write(field.Offset, &value, sizeof(value));
                    break;
                }
                case ShaderPassFieldKind::Int:
                case ShaderPassFieldKind::UInt:
                {
                    const int32_t value = data.Int(field.Name);
                    write(field.Offset, &value, sizeof(value));
                    break;
                }
                case ShaderPassFieldKind::Float:
                {
                    const float value = data.Float(field.Name);
                    write(field.Offset, &value, sizeof(value));
                    break;
                }
                case ShaderPassFieldKind::Float2:
                case ShaderPassFieldKind::Float3:
                case ShaderPassFieldKind::Float4:
                {
                    const glm::vec4 value = field.Kind == ShaderPassFieldKind::Float2 ? glm::vec4(data.Float2(field.Name), 0.0f, 0.0f)
                                          : field.Kind == ShaderPassFieldKind::Float3 ? glm::vec4(data.Float3(field.Name), 0.0f)
                                          : data.Float4(field.Name);
                    const size_t components = field.Kind == ShaderPassFieldKind::Float2 ? 2 : field.Kind == ShaderPassFieldKind::Float3 ? 3 : 4;
                    write(field.Offset, &value.x, components * sizeof(float));
                    break;
                }
                case ShaderPassFieldKind::Enum:
                {
                    const size_t index = std::min<size_t>(static_cast<size_t>(std::max(data.Int(field.Name), 0)), field.EnumValues.size() - 1);
                    const int32_t value = static_cast<int32_t>(field.EnumValues[index]);
                    write(field.Offset, &value, sizeof(value));
                    break;
                }
            }
        }

        if (pass.Compute)
        {
            if (reflection.DispatchThreads[0] != 0)
            {
                dispatchSize = {reflection.DispatchThreads[0], reflection.DispatchThreads[1], reflection.DispatchThreads[2]};
            }
            else if (!reflection.DispatchSizeOf.empty())
            {
                const GraphTexture texture = data.Texture(reflection.DispatchSizeOf);
                dispatchSize = texture.Valid() ? context.Size(texture) : Dimensions{0, 0, 0};
            }
            if (dispatchSize.Width == 0 || dispatchSize.Height == 0 || dispatchSize.Depth == 0) return;

            cmdBindComputePipeline(context.Cmd, pass.ComputePipeline);
            cmdPushConstants(context.Cmd, constants.data(), program.PushConstantSize);
            cmdDispatchThreads(context.Cmd, dispatchSize);
            return;
        }

        if (reflection.DrawScene && !scene)
        {
            if (!pass.ReportedMissingScene) Logger->error("Render graph pass '{}' ({}) draws the scene, but what is connected to its scene pin is not one", data.Name(), pass.Module);
            pass.ReportedMissingScene = true;
            return;
        }

        if (depthFormat != Format::Invalid) cmdSetDepthState(context.Cmd, depthState);
        if (scene) cmdBindIndexBuffer(context.Cmd, scene->IndexBuffer, IndexFormat::UI32);

        for (uint32_t fragment = 0; fragment < reflection.Fragments.size(); ++fragment)
        {
            cmdBindRenderPipeline(context.Cmd, GetRasterPipeline(pass, targetFormats, targetCount, depthFormat, fragment));
            if (program.PushConstantSize > 0) cmdPushConstants(context.Cmd, constants.data(), program.PushConstantSize);

            if (!reflection.DrawScene)
            {
                cmdDraw(context.Cmd, 3);
                continue;
            }

            // One indirect draw per run of instances whose alpha mode the fragment shader draws: instances are sorted
            // by alpha mode, and each draw's firstInstance is its instance's index.
            const uint8_t materials = reflection.Fragments[fragment].Materials;
            for (uint32_t mode = 0; mode < 3;)
            {
                if (!(materials & (1u << mode)))
                {
                    ++mode;
                    continue;
                }

                const uint32_t first = scene->FirstInstance[mode];
                uint32_t count = 0;
                for (; mode < 3 && (materials & (1u << mode)); ++mode) count += scene->InstanceCount[mode];
                if (count > 0) cmdDrawIndexedIndirect(context.Cmd, scene->IndirectBuffer, first * sizeof(DrawIndexedIndirectCommand), count);
            }
        }
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
            Resolver(PassRegistryData& registry, const GraphFileDescription& file, GraphFileData::Version& out)
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

            PassRegistryData& Registry;
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

                const uint32_t typeIndex = Registry.FindOrLoadType(typeName);
                if (typeIndex == kNone)
                {
                    std::string known;
                    for (const PassRegistryData::Type& type : Registry.Types)
                    {
                        if (*type.Name) known += fmt::format("{}{}", known.empty() ? "" : ", ", type.Name);
                    }
                    Error(typeSetting->Location, fmt::format("unknown pass type '{}': no pass type of that name is registered ({}) and no Slang pass module of that name loads",
                                                             typeName, known.empty() ? "none" : known));
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
                    case PropertyType::String:
                    {
                        std::string_view text;
                        if (ReadSingle(setting, text)) value.String = Out.Intern(text);
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
                    const std::string_view outputType = output.BufferType;
                    const std::string_view inputType = input.BufferType;
                    if (!outputType.empty() && !inputType.empty() && outputType != inputType)
                    {
                        Error(location, fmt::format("'{}' is a {} and '{}' takes a {}", Describe(source), outputType, Describe(destination), inputType));
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
        // A reload of shaders and graph files together: the file has to see the passes as they are now.
        Registry.Refresh();
        ResolvedGeneration = Registry.Generation;
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

    bool GraphFileData::LoadFromDisk()
    {
        LastLoadFailed = true;
        auto file = std::make_unique<ParsedGraphFile>(ParseGraphFile(Path));
        if (!file->Errors.empty())
        {
            Logger->error("Render graph file {} has errors{}:\n{}", file->Path, Current ? "; the previous version stays in use" : "", fmt::join(file->Errors, "\n"));
            return false;
        }
        if (!Resolve(file->Description())) return false;

        Parsed = std::move(file);
        LastLoadFailed = false;
        return true;
    }

    // -------------------------------------------------------------------------------------------------------------------
    // Adding a file's passes to a frame

    void GraphFileData::AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources)
    {
        // A hot reload changed the pins or properties of a Slang pass: the version in use refers to its old type.
        Registry.Refresh();
        if (Registry.Generation != ResolvedGeneration && (Current || Pending))
        {
            ResolvedGeneration = Registry.Generation;
            Pending.reset();
            if (!Resolve(Parsed->Description()))
            {
                Current.reset();
                Logger->error("Render graph file {}: its passes do not run until it or the passes are fixed", Path.generic_string());
            }
        }

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
        HostCopies.clear();

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

            // Outputs: what the type's Setup hands out, an application resource, or one the graph creates.
            if (type.Setup)
            {
                const PassData data(*this, passIndex);
                PassSetup setup(graph, data, *this, passIndex);
                type.Setup(setup);
            }

            for (uint32_t pin = 0; pin < type.PinCount; ++pin)
            {
                const PassRegistryData::Pin& description = Registry.Pins[type.FirstPin + pin];
                const Slot& slot = version.Slots[pass.FirstSlot + pin];
                if (description.Direction != PinDirection::Output) continue;

                Resource& resource = SlotResources[pass.FirstSlot + pin];
                if (resource.Valid()) continue;
                if (slot.TargetResource != kNone)
                {
                    resource = applicationResource(slot.TargetResource, description, slot.DebugName);
                    continue;
                }

                if (IsBufferUsage(description.Usage))
                {
                    if (description.BufferSize > 0) resource.Buffer = graph.CreateBuffer({.Size = description.BufferSize, .DebugName = slot.DebugName});
                    else ReportOnce(fmt::format("'{}' has no buffer: its pass type's Setup gave it none", slot.DebugName));
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

                if (description.Width > 0)
                {
                    texture.Size = {.Width = description.Width, .Height = description.Height, .Depth = 1};
                }
                else if (description.SizeOf != kNone)
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

            // A type with only Setup records nothing itself.
            if (!type.Execute) continue;

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

    const SceneDrawData* PassData::Scene(std::string_view pin) const
    {
        const uint32_t slot = FindPin(pin);
        return slot == kNone ? nullptr : File.SlotResources[slot].Scene;
    }

    const void* PassData::HostData(std::string_view pin, size_t size) const
    {
        const uint32_t slot = FindPin(pin);
        if (slot == kNone) return nullptr;

        const GraphFileData::Resource& resource = File.SlotResources[slot];
        return resource.HostSize == size ? resource.Host : nullptr;
    }

    bool PassData::Bool(std::string_view property) const { return FindProperty(property, PropertyType::Bool).Bool; }
    int32_t PassData::Int(std::string_view property) const { return FindProperty(property, PropertyType::Int).Int; }
    float PassData::Float(std::string_view property) const { return FindProperty(property, PropertyType::Float).Float.x; }
    glm::vec2 PassData::Float2(std::string_view property) const { return glm::vec2(FindProperty(property, PropertyType::Float2).Float); }
    glm::vec3 PassData::Float3(std::string_view property) const { return glm::vec3(FindProperty(property, PropertyType::Float3).Float); }
    glm::vec4 PassData::Float4(std::string_view property) const { return FindProperty(property, PropertyType::Float4).Float; }
    const char* PassData::String(std::string_view property) const { return FindProperty(property, PropertyType::String).String; }

    // -------------------------------------------------------------------------------------------------------------------
    // PassSetup

    void PassSetup::Output(std::string_view pin, GraphTexture texture)
    {
        const uint32_t slot = Data.FindPin(pin);
        if (slot == kNone) return;

        const GraphFileData::Pass& pass = File.Current->Passes[Pass];
        const PassRegistryData::Pin& description = File.Registry.Pins[File.Registry.Types[pass.Type].FirstPin + (slot - pass.FirstSlot)];
        CHECK_RETURN(description.Direction == PinDirection::Output && !IsBufferUsage(description.Usage), "Pin '{}' of pass type '{}' is not a texture output", pin, File.Registry.Types[pass.Type].Name);
        File.SlotResources[slot] = {.Texture = texture};
    }

    void PassSetup::Output(std::string_view pin, GraphBuffer buffer, const SceneDrawData* scene)
    {
        const uint32_t slot = Data.FindPin(pin);
        if (slot == kNone) return;

        const GraphFileData::Pass& pass = File.Current->Passes[Pass];
        const PassRegistryData::Pin& description = File.Registry.Pins[File.Registry.Types[pass.Type].FirstPin + (slot - pass.FirstSlot)];
        CHECK_RETURN(description.Direction == PinDirection::Output && IsBufferUsage(description.Usage), "Pin '{}' of pass type '{}' is not a buffer output", pin, File.Registry.Types[pass.Type].Name);
        File.SlotResources[slot] = {.Buffer = buffer, .Scene = scene};
    }

    void PassSetup::Output(std::string_view pin, GraphBuffer buffer, const void* host, size_t size)
    {
        Output(pin, buffer);
        const uint32_t slot = Data.FindPin(pin);
        if (slot == kNone) return;

        std::byte* copy = File.HostCopies.emplace_back(std::make_unique<std::byte[]>(size)).get();
        std::memcpy(copy, host, size);
        File.SlotResources[slot].Host = copy;
        File.SlotResources[slot].HostSize = size;
    }

    // -------------------------------------------------------------------------------------------------------------------
    // GraphFile

    GraphFile::GraphFile(PassRegistry& registry, std::filesystem::path path)
    : File(std::make_unique<GraphFileData>(*registry.Types, std::move(path)))
    {
        // A file with errors is reported; its passes run once a Reload() finds it fixed.
        std::error_code error;
        File->LastWriteTime = std::filesystem::last_write_time(File->Path, error);
        if (File->LoadFromDisk()) File->Current = std::move(File->Pending);
    }

    GraphFile::~GraphFile() = default;

    bool GraphFile::Reload()
    {
        // An editor that saves by replacing the file leaves a moment where it does not exist; the next call sees it.
        // A file that had errors is read again even unchanged: they may have been in a Slang pass fixed since.
        std::error_code error;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(File->Path, error);
        if (error || (writeTime == File->LastWriteTime && !File->LastLoadFailed)) return false;

        File->LastWriteTime = writeTime;
        return File->LoadFromDisk();
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

    namespace
    {
        // The version in use and a reloaded one waiting for the next AddTo: changes made now apply to both, so a reload
        // does not undo them for a frame.
        template<typename Function>
        bool ForEachVersion(GraphFileData& file, Function&& function)
        {
            bool found = false;
            if (file.Current) found |= function(*file.Current);
            if (file.Pending) found |= function(*file.Pending);
            return found;
        }

        [[nodiscard]] PropertyValue* FindValue(const PassRegistryData& registry, GraphFileData::Version& version, std::string_view passName, std::string_view propertyName, PropertyType type)
        {
            const uint32_t passIndex = version.FindPass(passName);
            if (passIndex == kNone) return nullptr;

            const GraphFileData::Pass& pass = version.Passes[passIndex];
            const PassRegistryData::Type& passType = registry.Types[pass.Type];
            const uint32_t property = registry.FindProperty(passType, propertyName);
            if (property == kNone) return nullptr;

            const PropertyType actual = registry.Properties[passType.FirstProperty + property].Type;
            if (actual != type && !(type == PropertyType::Int && actual == PropertyType::Choice)) return nullptr;
            return &version.Values[pass.FirstValue + property];
        }

        template<typename Assign>
        bool SetValue(GraphFileData& file, std::string_view pass, std::string_view property, PropertyType type, Assign&& assign)
        {
            return ForEachVersion(file, [&](GraphFileData::Version& version)
            {
                PropertyValue* stored = FindValue(file.Registry, version, pass, property, type);
                if (stored) assign(*stored);
                return stored != nullptr;
            });
        }
    }

    bool GraphFile::SetEnabled(std::string_view pass, bool enabled)
    {
        return ForEachVersion(*File, [pass, enabled](GraphFileData::Version& version)
        {
            const uint32_t index = version.FindPass(pass);
            if (index != kNone) version.Passes[index].Enabled = enabled;
            return index != kNone;
        });
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, bool value)
    {
        return SetValue(*File, pass, property, PropertyType::Bool, [value](PropertyValue& stored) { stored.Bool = value; });
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, int32_t value)
    {
        return SetValue(*File, pass, property, PropertyType::Int, [value](PropertyValue& stored) { stored.Int = value; });
    }

    bool GraphFile::SetProperty(std::string_view pass, std::string_view property, float value)
    {
        return SetValue(*File, pass, property, PropertyType::Float, [value](PropertyValue& stored) { stored.Float.x = value; });
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
        return {.Name = entry.Name, .Type = type.Name, .Enabled = &entry.Enabled, .PinCount = type.PinCount, .PropertyCount = type.PropertyCount};
    }

    GraphFilePin GraphFile::GetPin(uint32_t pass, uint32_t pin) const
    {
        CHECK(pass < GetPassCount(), "No pass {} in the graph file", pass);
        const GraphFileData::Pass& entry = File->Current->Passes[pass];
        const PassRegistryData::Type& type = File->Registry.Types[entry.Type];
        CHECK(pin < type.PinCount, "Pass '{}' has no pin {}", entry.Name, pin);

        const PassRegistryData::Pin& description = File->Registry.Pins[type.FirstPin + pin];
        return {.Name = description.Name, .Direction = description.Direction, .Usage = description.Usage};
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
