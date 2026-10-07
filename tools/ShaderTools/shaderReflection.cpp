#include "shaderReflection.h"

#if defined(EOS_SHADER_TOOLS)
#include <algorithm>
#include <cmath>

#include "formatNames.h"

// Follows the traversal recommended by Slang's reflection guide (docs/user-guide/09-reflection.md):
// start from getGlobalParamsVarLayout() / EntryPointReflection::getVarLayout() rather than the flat parameter lists,
// look through ConstantBuffer / PushConstant containers with getElementVarLayout(), and treat every offset as relative
// to its parent, summing along the path.
namespace EOS::SlangReflection
{
    namespace
    {
        using Kind = slang::TypeReflection::Kind;

        [[nodiscard]] const char* NameOrUnnamed(const char* name, const char* fallback = "<unnamed>")
        {
            return name ? name : fallback;
        }

        [[nodiscard]] ShaderScalarType ToScalarType(slang::TypeReflection::ScalarType scalarType)
        {
            switch (scalarType)
            {
                case slang::TypeReflection::Bool:    return ShaderScalarType::Boolean;
                case slang::TypeReflection::Int8:    return ShaderScalarType::Int8;
                case slang::TypeReflection::UInt8:   return ShaderScalarType::UInt8;
                case slang::TypeReflection::Int16:   return ShaderScalarType::Int16;
                case slang::TypeReflection::UInt16:  return ShaderScalarType::UInt16;
                case slang::TypeReflection::Int32:   return ShaderScalarType::Int32;
                case slang::TypeReflection::UInt32:  return ShaderScalarType::UInt32;
                case slang::TypeReflection::Int64:   return ShaderScalarType::Int64;
                case slang::TypeReflection::UInt64:  return ShaderScalarType::UInt64;
                case slang::TypeReflection::Float16: return ShaderScalarType::Float16;
                case slang::TypeReflection::Float32: return ShaderScalarType::Float32;
                case slang::TypeReflection::Float64: return ShaderScalarType::Float64;
                default:                             return ShaderScalarType::Unknown;
            }
        }

        [[nodiscard]] ShaderResourceType ToResourceType(slang::TypeLayoutReflection* typeLayout)
        {
            // eos.bindless declares its arrays as Slang's untyped __DynamicResource, aliased per use.
            slang::TypeLayoutReflection* elementTypeLayout = typeLayout->getKind() == Kind::Array ? typeLayout->getElementTypeLayout() : typeLayout;
            if (elementTypeLayout->getKind() == Kind::DynamicResource) return ShaderResourceType::Bindless;

            if (typeLayout->getBindingRangeCount() == 0) return ShaderResourceType::Unknown;

            switch (typeLayout->getBindingRangeType(0))
            {
                case slang::BindingType::Sampler:                         return ShaderResourceType::Sampler;
                case slang::BindingType::Texture:                         return ShaderResourceType::SampledTexture;
                case slang::BindingType::MutableTexture:                  return ShaderResourceType::StorageTexture;
                case slang::BindingType::CombinedTextureSampler:          return ShaderResourceType::CombinedTextureSampler;
                case slang::BindingType::ConstantBuffer:
                case slang::BindingType::ParameterBlock:                  return ShaderResourceType::UniformBuffer;
                case slang::BindingType::RawBuffer:
                case slang::BindingType::MutableRawBuffer:                return ShaderResourceType::StorageBuffer;
                case slang::BindingType::RayTracingAccelerationStructure: return ShaderResourceType::AccelerationStructure;
                default:                                                  return ShaderResourceType::Unknown;
            }
        }

        [[nodiscard]] bool HasCategory(slang::VariableLayoutReflection* variable, slang::ParameterCategory category)
        {
            for (unsigned int i = 0; i < variable->getCategoryCount(); ++i)
            {
                if (variable->getCategoryByIndex(i) == category) return true;
            }

            return false;
        }

        void AddVarying(std::vector<ShaderVarying>& outVaryings, const char* name, const char* semantic, size_t semanticIndex, uint32_t location, slang::TypeLayoutReflection* typeLayout)
        {
            ShaderVarying varying
            {
                .Name = NameOrUnnamed(name, "result"),     // only an entry point's return value has no name
                .Semantic = semantic ? semantic : "",
                .SemanticIndex = static_cast<uint32_t>(semanticIndex),
                .Location = location,
            };

            switch (typeLayout->getKind())
            {
                case Kind::Scalar:
                    varying.ComponentType = ToScalarType(typeLayout->getScalarType());
                    varying.ComponentCount = 1;
                    break;
                case Kind::Vector:
                    varying.ComponentType = ToScalarType(typeLayout->getElementTypeLayout()->getScalarType());
                    varying.ComponentCount = static_cast<uint8_t>(typeLayout->getElementCount());
                    break;
                default:
                    break;
            }

            outVaryings.push_back(std::move(varying));
        }

        // Collects the leaves of a stage input or output (struct members, array elements, matrix rows) with their
        // absolute locations. System values (SV_Position, SV_VertexID, ...) have no location and are skipped.
        void CollectVaryings(slang::VariableLayoutReflection* variable, slang::ParameterCategory category, uint32_t parentLocation, std::vector<ShaderVarying>& outVaryings)
        {
            if (!variable || !HasCategory(variable, category)) return;

            const uint32_t location = parentLocation + static_cast<uint32_t>(variable->getOffset(category));
            slang::TypeLayoutReflection* typeLayout = variable->getTypeLayout();

            switch (typeLayout->getKind())
            {
                case Kind::Struct:
                {
                    for (unsigned int i = 0; i < typeLayout->getFieldCount(); ++i)
                    {
                        CollectVaryings(typeLayout->getFieldByIndex(i), category, location, outVaryings);
                    }
                    return;
                }
                case Kind::Array:
                {
                    slang::TypeLayoutReflection* elementTypeLayout = typeLayout->getElementTypeLayout();

                    // Per-vertex inputs of geometry and tessellation stages (`VSOut input[3]`): every vertex reads the
                    // same locations, so the members are listed once.
                    if (elementTypeLayout->getKind() == Kind::Struct)
                    {
                        for (unsigned int i = 0; i < elementTypeLayout->getFieldCount(); ++i)
                        {
                            CollectVaryings(elementTypeLayout->getFieldByIndex(i), category, location, outVaryings);
                        }
                        return;
                    }

                    const uint32_t stride = static_cast<uint32_t>(std::max<size_t>(typeLayout->getElementStride(static_cast<SlangParameterCategory>(category)), 1));
                    for (size_t i = 0; i < typeLayout->getElementCount(); ++i)
                    {
                        AddVarying(outVaryings, variable->getName(), variable->getSemanticName(), variable->getSemanticIndex() + i, location + static_cast<uint32_t>(i) * stride, elementTypeLayout);
                    }
                    return;
                }
                case Kind::Matrix:
                {
                    // A matrix varying takes one location per row.
                    slang::TypeLayoutReflection* rowTypeLayout = typeLayout->getElementTypeLayout();
                    for (unsigned int row = 0; row < typeLayout->getRowCount(); ++row)
                    {
                        AddVarying(outVaryings, variable->getName(), variable->getSemanticName(), variable->getSemanticIndex() + row, location + row, rowTypeLayout);
                    }
                    return;
                }
                default:
                    AddVarying(outVaryings, variable->getName(), variable->getSemanticName(), variable->getSemanticIndex(), location, typeLayout);
                    return;
            }
        }

        // The stride, not the size: a struct starting with a pointer has 8-byte alignment, so its C++ twin (and the
        // generated header) is padded to a multiple of 8 and pushed whole, while Slang's size stops at the last field.
        [[nodiscard]] uint32_t GetPushConstantBlockSize(slang::TypeLayoutReflection* containerTypeLayout)
        {
            slang::VariableLayoutReflection* element = containerTypeLayout->getElementVarLayout();
            return element ? static_cast<uint32_t>(element->getTypeLayout()->getStride()) : 0;
        }

        // Global scope: [[vk::push_constant]] blocks, [[vk::constant_id]] constants and descriptor bindings.
        [[nodiscard]] bool ReflectGlobalScope(slang::ProgramLayout* layout, CompiledShaderProgram& program, uint32_t& outPushConstantSize,
                                              slang::TypeLayoutReflection*& outPushConstantStruct, std::string& outDiagnostics)
        {
            bool success = true;
            slang::TypeLayoutReflection* scopeTypeLayout = layout->getGlobalParamsVarLayout()->getTypeLayout();

            // Slang wraps global-scope ordinary data (`uniform float x;` outside any block) in an implicit constant
            // buffer. EOS's pipeline layout only has the bindless set and push constants, so it has nowhere to go.
            if (scopeTypeLayout->getKind() == Kind::ConstantBuffer || scopeTypeLayout->getKind() == Kind::ParameterBlock)
            {
                scopeTypeLayout = scopeTypeLayout->getElementVarLayout()->getTypeLayout();
            }

            uint32_t pushConstantBlocks = 0;
            for (unsigned int i = 0; i < scopeTypeLayout->getFieldCount(); ++i)
            {
                slang::VariableLayoutReflection* field = scopeTypeLayout->getFieldByIndex(i);
                const char* name = NameOrUnnamed(field->getName());

                for (unsigned int c = 0; c < field->getCategoryCount(); ++c)
                {
                    const slang::ParameterCategory category = field->getCategoryByIndex(c);
                    switch (category)
                    {
                        case slang::ParameterCategory::PushConstantBuffer:
                            ++pushConstantBlocks;
                            outPushConstantSize = GetPushConstantBlockSize(field->getTypeLayout());
                            outPushConstantStruct = field->getTypeLayout()->getElementTypeLayout();
                            break;

                        case slang::ParameterCategory::SpecializationConstant:
                            program.SpecializationConstants.push_back(
                            {
                                .Name = name,
                                .ConstantID = static_cast<uint32_t>(field->getOffset(category)),
                                .Type = ToScalarType(field->getTypeLayout()->getScalarType()),
                            });
                            break;

                        case slang::ParameterCategory::DescriptorTableSlot:
                            program.ResourceBindings.push_back(
                            {
                                .Name = name,
                                .Set = static_cast<uint32_t>(field->getBindingSpace(category)),
                                .Binding = static_cast<uint32_t>(field->getOffset(category)),
                                .Type = ToResourceType(field->getTypeLayout()),
                            });
                            break;

                        case slang::ParameterCategory::Uniform:
                            outDiagnostics += std::string("error: global uniform '") + name + "' in module '" + program.Description.Module
                                            + "' needs an implicit uniform buffer, which EOS does not bind. Move it into the push constants "
                                              "(a [[vk::push_constant]] struct or a `uniform` entry-point parameter) or into a buffer read through a pointer.\n";
                            success = false;
                            break;

                        default:
                            break;
                    }
                }
            }

            if (pushConstantBlocks > 1)
            {
                outDiagnostics += "error: module '" + program.Description.Module + "' declares " + std::to_string(pushConstantBlocks)
                                + " [[vk::push_constant]] blocks; Vulkan allows one per entry point. Merge them into one struct.\n";
                success = false;
            }

            return success;
        }

        // An entry point's `uniform` parameters become its own push-constant block on Vulkan.
        [[nodiscard]] bool ReflectEntryPointScope(slang::EntryPointReflection* entryPointLayout, ShaderEntryPoint& entryPoint, std::string& outDiagnostics)
        {
            slang::VariableLayoutReflection* scope = entryPointLayout->getVarLayout();
            slang::TypeLayoutReflection* scopeTypeLayout = scope->getTypeLayout();
            slang::VariableLayoutReflection* parameters = scope;

            if (scopeTypeLayout->getKind() == Kind::ConstantBuffer || scopeTypeLayout->getKind() == Kind::ParameterBlock)
            {
                if (!HasCategory(scope, slang::ParameterCategory::PushConstantBuffer))
                {
                    outDiagnostics += "error: the uniform parameters of entry point '" + entryPoint.Name + "' were placed in a uniform buffer instead of push constants.\n";
                    return false;
                }

                entryPoint.PushConstantSize = GetPushConstantBlockSize(scopeTypeLayout);
                parameters = scopeTypeLayout->getElementVarLayout();
            }

            slang::TypeLayoutReflection* parametersTypeLayout = parameters->getTypeLayout();
            for (unsigned int i = 0; i < parametersTypeLayout->getFieldCount(); ++i)
            {
                slang::VariableLayoutReflection* parameter = parametersTypeLayout->getFieldByIndex(i);
                CollectVaryings(parameter, slang::ParameterCategory::VaryingInput, 0, entryPoint.Inputs);
                CollectVaryings(parameter, slang::ParameterCategory::VaryingOutput, 0, entryPoint.Outputs);   // `out` parameters
            }

            CollectVaryings(entryPointLayout->getResultVarLayout(), slang::ParameterCategory::VaryingOutput, 0, entryPoint.Outputs);

            auto byLocation = [](const ShaderVarying& lhs, const ShaderVarying& rhs) { return lhs.Location < rhs.Location; };
            std::ranges::sort(entryPoint.Inputs, byLocation);
            std::ranges::sort(entryPoint.Outputs, byLocation);
            return true;
        }
    }

    namespace
    {
        [[nodiscard]] std::string FullName(slang::TypeReflection* type)
        {
            Slang::ComPtr<ISlangBlob> blob;
            if (!type || SLANG_FAILED(type->getFullName(blob.writeRef())) || !blob) return {};
            return {static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
        }

        // Reads the eos.pass attributes of a [Pass] struct field or a color target into field. Returns false (with a
        // diagnostic) for an attribute that does not fit the field.
        [[nodiscard]] bool ReadPassAttributes(slang::VariableReflection* variable, ShaderPassField& field, const std::string& where, std::string& outDiagnostics)
        {
            bool success = true;
            const bool pin = field.Kind <= ShaderPassFieldKind::ColorTarget;
            const bool texturePin = field.Kind == ShaderPassFieldKind::Texture || field.Kind == ShaderPassFieldKind::StorageTexture
                                 || field.Kind == ShaderPassFieldKind::DepthTarget || field.Kind == ShaderPassFieldKind::ColorTarget;
            const bool property = field.Kind >= ShaderPassFieldKind::Bool;
            const auto error = [&](const std::string& message)
            {
                outDiagnostics += "error: " + where + ": " + message + "\n";
                success = false;
            };

            for (unsigned int i = 0; i < variable->getUserAttributeCount(); ++i)
            {
                slang::UserAttribute* attribute = variable->getUserAttributeByIndex(i);
                const std::string name = attribute->getName();

                const auto readString = [&](std::string& out)
                {
                    size_t size = 0;
                    const char* text = attribute->getArgumentValueString(0, &size);
                    out = text ? std::string(text, size) : std::string{};
                };
                const auto readFloat = [&](unsigned int index, float& out)
                {
                    if (SLANG_SUCCEEDED(attribute->getArgumentValueFloat(index, &out))) return;
                    int value = 0;
                    if (SLANG_SUCCEEDED(attribute->getArgumentValueInt(index, &value))) out = static_cast<float>(value);
                };
                const auto readInt = [&](unsigned int index) -> int
                {
                    int value = 0;
                    attribute->getArgumentValueInt(index, &value);
                    return value;
                };

                if (name == "Input" || name == "Output" || name == "InOut")
                {
                    if (!pin) error("[" + name + "] is for pins (textures, storage textures, pointers, depth and color targets)");
                    else if (field.Direction != ShaderPassDirection::None && field.Kind != ShaderPassFieldKind::ColorTarget) error("has more than one of [Input], [Output] and [InOut]");
                    field.Direction = name == "Input" ? ShaderPassDirection::Input : name == "Output" ? ShaderPassDirection::Output : ShaderPassDirection::InOut;
                }
                else if (name == "Optional")
                {
                    field.Optional = true;
                }
                else if (name == "Format")
                {
                    readString(field.Format);
                    const FormatName* format = FindFormat(field.Format);
                    if (!format) error("unknown format '" + field.Format + "'");
                    else if (IsDepthFormat(format->Value) != (field.Kind == ShaderPassFieldKind::DepthTarget)) error(field.Kind == ShaderPassFieldKind::DepthTarget ? "a DepthTarget has a depth format" : "only a DepthTarget has a depth format");
                    if (field.Format == "swapchain") field.Format.clear();
                }
                else if (name == "Scale")
                {
                    readFloat(0, field.Scale);
                    if (!(field.Scale > 0.0f)) error("[Scale] is larger than 0");
                }
                else if (name == "Size")
                {
                    if (!texturePin || field.Kind == ShaderPassFieldKind::ColorTarget) error("[Size] is for texture and depth outputs");
                    field.Width = static_cast<uint32_t>(std::max(readInt(0), 0));
                    field.Height = static_cast<uint32_t>(std::max(readInt(1), 0));
                    if (field.Width == 0 || field.Height == 0) error("[Size] is at least 1 by 1");
                }
                else if (name == "Layers")
                {
                    if (!texturePin || field.Kind == ShaderPassFieldKind::ColorTarget) error("[Layers] is for texture and depth outputs");
                    field.Layers = static_cast<uint32_t>(std::max(readInt(0), 0));
                    if (field.Layers == 0 || field.Layers > 255) error("[Layers] is 1 to 255");
                }
                else if (name == "SizeOf")
                {
                    readString(field.SizeOf);
                }
                else if (name == "Bypass")
                {
                    readString(field.Bypass);
                }
                else if (name == "Clear")
                {
                    for (unsigned int c = 0; c < 4; ++c) readFloat(c, field.Clear[c]);
                }
                else if (name == "DepthTest")
                {
                    if (field.Kind != ShaderPassFieldKind::DepthTarget && field.Kind != ShaderPassFieldKind::Texture) error("[DepthTest] is for a DepthTarget or a texture input attached as a read-only depth target");
                    field.DepthCompare = static_cast<int8_t>(std::clamp(readInt(0), 0, 7));
                }
                else if (name == "Sampler")
                {
                    if (field.Kind != ShaderPassFieldKind::Sampler) error("[Sampler] is for a DescriptorHandle<SamplerState>");
                    field.SamplerFilter = static_cast<uint8_t>(std::clamp(readInt(0), 0, 1));
                    field.SamplerAddress = static_cast<uint8_t>(std::clamp(readInt(1), 0, 3));
                }
                else if (name == "Range")
                {
                    if (!property || field.Kind == ShaderPassFieldKind::Bool || field.Kind == ShaderPassFieldKind::Enum) error("[Range] is for number properties");
                    readFloat(0, field.Minimum);
                    readFloat(1, field.Maximum);
                }
            }

            return success;
        }

        // Whether field is attached as the pass's depth target.
        [[nodiscard]] bool IsDepthPin(const ShaderPassField& field)
        {
            return field.Kind == ShaderPassFieldKind::DepthTarget || (field.Kind == ShaderPassFieldKind::Texture && field.DepthCompare >= 0);
        }

        // The [Pass] struct a module's push constants are, and the color targets of its fragment shaders.
        [[nodiscard]] bool ReflectPass(slang::TypeLayoutReflection* passLayout, slang::ProgramLayout* layout, CompiledShaderProgram& program, std::string& outDiagnostics)
        {
            ShaderPassReflection& pass = program.Pass;
            pass.IsPass = true;
            bool success = true;
            const std::string module = "pass '" + program.Description.Module + "'";

            for (unsigned int i = 0; i < passLayout->getFieldCount(); ++i)
            {
                slang::VariableLayoutReflection* fieldLayout = passLayout->getFieldByIndex(i);
                slang::VariableReflection* variable = fieldLayout->getVariable();
                slang::TypeReflection* type = variable->getType();
                ShaderPassField field{.Name = NameOrUnnamed(variable->getName()), .Offset = static_cast<uint32_t>(fieldLayout->getOffset())};
                const std::string where = module + ", field '" + field.Name + "'";
                const std::string typeName = FullName(type);

                bool supported = true;
                switch (type->getKind())
                {
                    case Kind::Struct:
                    {
                        // eos.pass's DepthTarget, or DescriptorHandle<T> of eos.bindless: what it refers to decides the pin.
                        if (typeName == "DepthTarget")
                        {
                            field.Kind = ShaderPassFieldKind::DepthTarget;
                            field.Format = "Z_F32";
                            break;
                        }
                        const std::string_view prefix = "DescriptorHandle<";
                        if (!typeName.starts_with(prefix))
                        {
                            supported = false;
                            break;
                        }
                        const std::string_view resource = std::string_view(typeName).substr(prefix.size());
                        if (resource.starts_with("RW")) field.Kind = ShaderPassFieldKind::StorageTexture;
                        else if (resource.starts_with("Texture") || resource.starts_with("Sampler2D")) field.Kind = ShaderPassFieldKind::Texture;
                        else if (resource.starts_with("SamplerState")) field.Kind = ShaderPassFieldKind::Sampler;
                        else supported = false;
                        break;
                    }
                    case Kind::Pointer:
                    {
                        slang::TypeLayoutReflection* pointee = fieldLayout->getTypeLayout()->getElementTypeLayout();
                        field.BufferSize = pointee ? static_cast<uint32_t>(pointee->getSize()) : 0;
                        field.BufferType = pointee ? FullName(pointee->getType()) : std::string{};
                        field.Kind = field.BufferType == "Scene" ? ShaderPassFieldKind::Scene : ShaderPassFieldKind::Buffer;
                        break;
                    }
                    case Kind::Scalar:
                    {
                        switch (type->getScalarType())
                        {
                            case slang::TypeReflection::Bool:    field.Kind = ShaderPassFieldKind::Bool; break;
                            case slang::TypeReflection::Int32:   field.Kind = ShaderPassFieldKind::Int; break;
                            case slang::TypeReflection::UInt32:  field.Kind = ShaderPassFieldKind::UInt; break;
                            case slang::TypeReflection::Float32: field.Kind = ShaderPassFieldKind::Float; break;
                            default:                             supported = false; break;
                        }
                        break;
                    }
                    case Kind::Vector:
                    {
                        const size_t count = type->getElementCount();
                        supported = type->getElementType()->getScalarType() == slang::TypeReflection::Float32 && count >= 2 && count <= 4;
                        field.Kind = count == 2 ? ShaderPassFieldKind::Float2 : count == 3 ? ShaderPassFieldKind::Float3 : ShaderPassFieldKind::Float4;
                        break;
                    }
                    case Kind::Enum:
                    {
                        field.Kind = ShaderPassFieldKind::Enum;
                        for (unsigned int c = 0; c < type->getFieldCount(); ++c)
                        {
                            slang::VariableReflection* enumCase = type->getFieldByIndex(c);
                            int64_t value = c;
                            enumCase->getDefaultValueInt(&value);
                            field.EnumNames.emplace_back(NameOrUnnamed(enumCase->getName()));
                            field.EnumValues.push_back(value);
                        }
                        supported = !field.EnumNames.empty() && fieldLayout->getTypeLayout()->getSize() == 4;
                        break;
                    }
                    default:
                        supported = false;
                        break;
                }

                if (!supported)
                {
                    outDiagnostics += "error: " + where + " is a " + typeName + "; a pass field is a DescriptorHandle of a texture, storage texture or "
                                      "sampler, a pointer (a buffer), a DepthTarget, or a bool, int, uint, float, float2-4 or enum property\n";
                    success = false;
                    continue;
                }

                if (!ReadPassAttributes(variable, field, where, outDiagnostics)) success = false;

                if (field.Kind == ShaderPassFieldKind::Texture)
                {
                    if (field.Direction == ShaderPassDirection::None) field.Direction = ShaderPassDirection::Input;
                    if (field.Direction != ShaderPassDirection::Input)
                    {
                        outDiagnostics += "error: " + where + ": a texture a pass writes is a DescriptorHandle<RWTexture*>, or a DepthTarget for depth\n";
                        success = false;
                    }
                }
                else if (field.Kind == ShaderPassFieldKind::Scene)
                {
                    if (field.Direction == ShaderPassDirection::None) field.Direction = ShaderPassDirection::Input;
                    if (field.Direction != ShaderPassDirection::Input)
                    {
                        outDiagnostics += "error: " + where + ": a scene is an input; gltfScene nodes provide it\n";
                        success = false;
                    }
                }
                else if ((field.Kind == ShaderPassFieldKind::StorageTexture || field.Kind == ShaderPassFieldKind::Buffer || field.Kind == ShaderPassFieldKind::DepthTarget)
                         && field.Direction == ShaderPassDirection::None)
                {
                    outDiagnostics += "error: " + where + " says whether the pass reads it ([Input]), creates it ([Output]) or changes it ([InOut])\n";
                    success = false;
                }
                if (field.Kind == ShaderPassFieldKind::StorageTexture && field.Direction == ShaderPassDirection::Input)
                {
                    outDiagnostics += "error: " + where + ": a texture a pass only reads is a DescriptorHandle<Texture*>\n";
                    success = false;
                }
                if (field.Kind == ShaderPassFieldKind::Buffer && field.Direction == ShaderPassDirection::Output && field.BufferSize == 0)
                {
                    outDiagnostics += "error: " + where + ": a buffer output points to something with a size\n";
                    success = false;
                }
                if (field.Direction != ShaderPassDirection::Output && (field.Width != 0 || field.Layers != 1))
                {
                    outDiagnostics += "error: " + where + ": [Size] and [Layers] shape outputs; an input is the size of what it is connected to\n";
                    success = false;
                }

                if (variable->hasDefaultValue())
                {
                    float floatValue = 0.0f;
                    int64_t intValue = 0;
                    if (field.Kind == ShaderPassFieldKind::Float && SLANG_SUCCEEDED(variable->getDefaultValueFloat(&floatValue)) && std::isfinite(floatValue)) field.Default = floatValue;
                    else if (SLANG_SUCCEEDED(variable->getDefaultValueInt(&intValue))) field.Default = static_cast<double>(intValue);
                }

                pass.Fields.push_back(std::move(field));
            }

            // Compute or raster, from the entry points.
            const ShaderEntryPoint* compute = nullptr;
            std::vector<SlangUInt> fragments;
            bool ownVertexShader = false;
            for (SlangUInt i = 0; i < program.EntryPoints.size(); ++i)
            {
                if (program.EntryPoints[i].Stage == ShaderStage::Compute) compute = &program.EntryPoints[i];
                if (program.EntryPoints[i].Stage == ShaderStage::Fragment) fragments.push_back(i);
                if (program.EntryPoints[i].Stage == ShaderStage::Vertex) ownVertexShader = true;
            }

            if (!compute == fragments.empty())
            {
                outDiagnostics += "error: " + module + " has " + (compute ? "both a compute and a fragment" : "neither a compute nor a fragment") + " entry point; a pass has one of them\n";
                return false;
            }

            // How a compute pass sizes its dispatch: attributes of the [Pass] struct, or its first storage texture output.
            slang::TypeReflection* passType = passLayout->getType();
            if (slang::UserAttribute* threads = passType->findUserAttributeByName("DispatchThreads"))
            {
                for (unsigned int axis = 0; axis < 3; ++axis)
                {
                    int count = 1;
                    threads->getArgumentValueInt(axis, &count);
                    pass.DispatchThreads[axis] = static_cast<uint32_t>(std::max(count, 0));
                }
                if (pass.DispatchThreads[0] == 0 || pass.DispatchThreads[1] == 0 || pass.DispatchThreads[2] == 0)
                {
                    outDiagnostics += "error: " + module + ": [DispatchThreads] dispatches at least one thread along every axis\n";
                    success = false;
                }
            }
            if (slang::UserAttribute* sizeOf = passType->findUserAttributeByName("DispatchSizeOf"))
            {
                size_t size = 0;
                const char* text = sizeOf->getArgumentValueString(0, &size);
                pass.DispatchSizeOf = text ? std::string(text, size) : std::string{};
                const auto pin = std::ranges::find(pass.Fields, pass.DispatchSizeOf, &ShaderPassField::Name);
                if (pin == pass.Fields.end() || (pin->Kind != ShaderPassFieldKind::Texture && pin->Kind != ShaderPassFieldKind::StorageTexture))
                {
                    outDiagnostics += "error: " + module + ": [DispatchSizeOf(\"" + pass.DispatchSizeOf + "\")] names no texture pin of the pass\n";
                    success = false;
                }
            }

            const bool sized = pass.DispatchThreads[0] != 0 || !pass.DispatchSizeOf.empty();
            if (!compute && sized)
            {
                outDiagnostics += "error: " + module + ": [DispatchThreads] and [DispatchSizeOf] are for compute passes\n";
                success = false;
            }
            if (compute && !sized && std::ranges::none_of(pass.Fields, [](const ShaderPassField& field) { return field.Kind == ShaderPassFieldKind::StorageTexture && field.Direction != ShaderPassDirection::Input; }))
            {
                outDiagnostics += "error: " + module + " is a compute pass without a storage texture it writes; size its dispatch with [DispatchSizeOf(\"pin\")] or [DispatchThreads(x, y, z)]\n";
                success = false;
            }

            // Rasterizer state, and the depth pin: at most one, raster passes only.
            if (slang::UserAttribute* cull = passType->findUserAttributeByName("Cull"))
            {
                int mode = 0;
                cull->getArgumentValueInt(0, &mode);
                pass.Cull = static_cast<uint8_t>(std::clamp(mode, 0, 2));
            }
            pass.DepthClamp = passType->findUserAttributeByName("DepthClamp") != nullptr;
            pass.DrawScene = passType->findUserAttributeByName("DrawScene") != nullptr;
            if (compute && (pass.Cull != 0 || pass.DepthClamp || pass.DrawScene))
            {
                outDiagnostics += "error: " + module + ": [Cull], [DepthClamp] and [DrawScene] are for raster passes\n";
                success = false;
            }

            const auto depthPins = std::ranges::count_if(pass.Fields, IsDepthPin);
            if (depthPins > 1 || (compute && depthPins > 0))
            {
                outDiagnostics += "error: " + module + (compute ? ": a compute pass has no depth target" : ": a pass has one depth target (a DepthTarget, or a texture input marked [DepthTest])") + "\n";
                success = false;
            }

            const auto scenes = std::ranges::count_if(pass.Fields, [](const ShaderPassField& field) { return field.Kind == ShaderPassFieldKind::Scene; });
            if (pass.DrawScene && scenes != 1)
            {
                std::string pointers;
                for (const ShaderPassField& field : pass.Fields)
                {
                    if (field.Kind == ShaderPassFieldKind::Buffer) pointers += (pointers.empty() ? "" : ", ") + field.Name + " (" + field.BufferType + "*)";
                }
                outDiagnostics += "error: " + module + ": a [DrawScene] pass has one Scene* pin (eos.scene), the scene it draws; it has " + std::to_string(scenes)
                                + (pointers.empty() ? "" : ", and these other pointers: " + pointers) + "\n";
                success = false;
            }
            if (pass.DrawScene && !ownVertexShader)
            {
                outDiagnostics += "error: " + module + ": a [DrawScene] pass has a vertex shader of its own, which reads the scene's vertices\n";
                success = false;
            }

            // The fragment entry points, with the materials each draws, and the color targets: the fields of the first
            // one's output struct, or its single return value. Every fragment entry point writes the same targets.
            std::vector<std::string> targetNames;
            for (size_t f = 0; f < fragments.size(); ++f)
            {
                slang::EntryPointReflection* entryPoint = layout->getEntryPointByIndex(fragments[f]);
                ShaderPassFragment& fragment = pass.Fragments.emplace_back(ShaderPassFragment{.EntryPoint = program.EntryPoints[fragments[f]].Name});
                const std::string where = module + ", fragment shader '" + fragment.EntryPoint + "'";

                uint8_t materials = 0;
                slang::FunctionReflection* function = entryPoint->getFunction();
                for (unsigned int a = 0; function && a < function->getUserAttributeCount(); ++a)
                {
                    slang::UserAttribute* attribute = function->getUserAttributeByIndex(a);
                    if (std::string_view(attribute->getName()) != "Materials") continue;
                    int mode = 0;
                    attribute->getArgumentValueInt(0, &mode);
                    materials |= static_cast<uint8_t>(1u << std::clamp(mode, 0, 2));
                }
                if (materials != 0)
                {
                    fragment.Materials = materials;
                    if (!pass.DrawScene)
                    {
                        outDiagnostics += "error: " + where + ": [Materials] is for passes that draw the scene ([DrawScene])\n";
                        success = false;
                    }
                }
                else if (fragments.size() > 1)
                {
                    outDiagnostics += "error: " + where + ": with several fragment shaders, each says which materials it draws ([Materials])\n";
                    success = false;
                }
                if (fragments.size() > 1 && !pass.DrawScene)
                {
                    outDiagnostics += "error: " + where + ": a pass that does not draw the scene has one fragment shader\n";
                    success = false;
                }
                for (size_t other = 0; other < f; ++other)
                {
                    if (pass.Fragments[other].Materials & fragment.Materials)
                    {
                        outDiagnostics += "error: " + where + " draws materials '" + pass.Fragments[other].EntryPoint + "' draws as well\n";
                        success = false;
                    }
                }

                std::vector<std::string> names;
                slang::VariableLayoutReflection* result = entryPoint->getResultVarLayout();
                slang::TypeLayoutReflection* resultLayout = result ? result->getTypeLayout() : nullptr;
                if (resultLayout && resultLayout->getKind() == Kind::Struct)
                {
                    for (unsigned int t = 0; t < resultLayout->getFieldCount(); ++t)
                    {
                        slang::VariableLayoutReflection* targetLayout = resultLayout->getFieldByIndex(t);
                        if (!HasCategory(targetLayout, slang::ParameterCategory::VaryingOutput)) continue;   // SV_Depth and the like

                        ShaderPassField target
                        {
                            .Name = NameOrUnnamed(targetLayout->getVariable()->getName()),
                            .Kind = ShaderPassFieldKind::ColorTarget,
                            .Offset = static_cast<uint32_t>(targetLayout->getOffset(slang::ParameterCategory::VaryingOutput)),
                        };
                        names.push_back(target.Name);
                        if (f != 0) continue;

                        if (!ReadPassAttributes(targetLayout->getVariable(), target, module + ", color target '" + target.Name + "'", outDiagnostics)) success = false;
                        if (target.Direction == ShaderPassDirection::None) target.Direction = ShaderPassDirection::Output;
                        if (target.Direction == ShaderPassDirection::Input)
                        {
                            outDiagnostics += "error: " + module + ", color target '" + target.Name + "' is written: [Output] or [InOut]\n";
                            success = false;
                        }
                        pass.Fields.push_back(std::move(target));
                    }
                }
                else if (result && HasCategory(result, slang::ParameterCategory::VaryingOutput))
                {
                    names.emplace_back("output");
                    if (f == 0) pass.Fields.push_back({.Name = "output", .Kind = ShaderPassFieldKind::ColorTarget, .Direction = ShaderPassDirection::Output});
                }

                if (f == 0) targetNames = std::move(names);
                else if (names != targetNames)
                {
                    outDiagnostics += "error: " + where + " writes other color targets than '" + pass.Fragments[0].EntryPoint + "'; the fragment shaders of a pass write the same ones\n";
                    success = false;
                }
            }

            // [SizeOf] and [Bypass] name another pin of the pass.
            for (const ShaderPassField& field : pass.Fields)
            {
                const auto find = [&pass](const std::string& name) { return std::ranges::find(pass.Fields, name, &ShaderPassField::Name); };
                const std::string where = module + ", '" + field.Name + "'";
                if (!field.SizeOf.empty())
                {
                    const auto source = find(field.SizeOf);
                    if (source == pass.Fields.end() || source->Kind != ShaderPassFieldKind::Texture)
                    {
                        outDiagnostics += "error: " + where + " is sized like '" + field.SizeOf + "', which is not a texture input of the pass\n";
                        success = false;
                    }
                }
                if (!field.Bypass.empty())
                {
                    const auto source = find(field.Bypass);
                    const bool fieldIsBuffer = field.Kind == ShaderPassFieldKind::Buffer;
                    if (source == pass.Fields.end() || source->Direction == ShaderPassDirection::Output || source->Direction == ShaderPassDirection::None
                        || (source->Kind == ShaderPassFieldKind::Buffer) != fieldIsBuffer || source->Kind == ShaderPassFieldKind::DepthTarget)
                    {
                        outDiagnostics += "error: " + where + " bypasses to '" + field.Bypass + "', which is not an input of the same kind\n";
                        success = false;
                    }
                }
            }

            return success;
        }
    }

    ShaderStage ToShaderStage(SlangStage stage)
    {
        switch (stage)
        {
            case SLANG_STAGE_VERTEX:         return ShaderStage::Vertex;
            case SLANG_STAGE_HULL:           return ShaderStage::Hull;
            case SLANG_STAGE_DOMAIN:         return ShaderStage::Domain;
            case SLANG_STAGE_GEOMETRY:       return ShaderStage::Geometry;
            case SLANG_STAGE_FRAGMENT:       return ShaderStage::Fragment;
            case SLANG_STAGE_COMPUTE:        return ShaderStage::Compute;
            case SLANG_STAGE_RAY_GENERATION: return ShaderStage::RayGen;
            case SLANG_STAGE_INTERSECTION:   return ShaderStage::Intersection;
            case SLANG_STAGE_ANY_HIT:        return ShaderStage::AnyHit;
            case SLANG_STAGE_CLOSEST_HIT:    return ShaderStage::ClosestHit;
            case SLANG_STAGE_MISS:           return ShaderStage::Miss;
            case SLANG_STAGE_CALLABLE:       return ShaderStage::Callable;
            case SLANG_STAGE_MESH:           return ShaderStage::Mesh;
            case SLANG_STAGE_AMPLIFICATION:  return ShaderStage::Amplification;
            default:                         return ShaderStage::None;
        }
    }

    bool Reflect(slang::IComponentType* linkedProgram, CompiledShaderProgram& program, std::string& outDiagnostics)
    {
        slang::ProgramLayout* layout = linkedProgram->getLayout();
        if (!layout)
        {
            outDiagnostics += "error: Slang produced no reflection for module '" + program.Description.Module + "'.\n";
            return false;
        }

        uint32_t globalPushConstantSize = 0;
        slang::TypeLayoutReflection* pushConstantStruct = nullptr;
        bool success = ReflectGlobalScope(layout, program, globalPushConstantSize, pushConstantStruct, outDiagnostics);

        const SlangUInt entryPointCount = layout->getEntryPointCount();
        if (entryPointCount != program.EntryPoints.size())
        {
            outDiagnostics += "error: reflection of module '" + program.Description.Module + "' reports a different number of entry points than were compiled.\n";
            return false;
        }

        program.PushConstantSize = globalPushConstantSize;
        for (SlangUInt i = 0; i < entryPointCount; ++i)
        {
            slang::EntryPointReflection* entryPointLayout = layout->getEntryPointByIndex(i);
            ShaderEntryPoint& entryPoint = program.EntryPoints[i];
            entryPoint.Name = entryPointLayout->getName();
            entryPoint.Stage = ToShaderStage(entryPointLayout->getStage());

            if (entryPoint.Stage == ShaderStage::Compute || entryPoint.Stage == ShaderStage::Mesh || entryPoint.Stage == ShaderStage::Amplification)
            {
                SlangUInt threadGroupSize[3]{};
                entryPointLayout->getComputeThreadGroupSize(3, threadGroupSize);
                for (int axis = 0; axis < 3; ++axis) entryPoint.ThreadGroupSize[axis] = static_cast<uint32_t>(threadGroupSize[axis]);
            }

            if (!ReflectEntryPointScope(entryPointLayout, entryPoint, outDiagnostics))
            {
                success = false;
                continue;
            }

            if (entryPoint.PushConstantSize > 0 && globalPushConstantSize > 0)
            {
                outDiagnostics += "error: entry point '" + entryPoint.Name + "' has `uniform` parameters and module '" + program.Description.Module
                                + "' also declares a [[vk::push_constant]] block; Vulkan allows one push-constant block per entry point. Use one or the other.\n";
                success = false;
            }

            if (entryPoint.PushConstantSize == 0) entryPoint.PushConstantSize = globalPushConstantSize;
            program.PushConstantSize = std::max(program.PushConstantSize, entryPoint.PushConstantSize);

            // Which descriptor bindings this entry point actually touches, after dead-code elimination.
            Slang::ComPtr<slang::IMetadata> metadata;
            Slang::ComPtr<ISlangBlob> diagnostics;
            if (SLANG_SUCCEEDED(linkedProgram->getEntryPointMetadata(static_cast<SlangInt>(i), 0, metadata.writeRef(), diagnostics.writeRef())) && metadata)
            {
                for (ShaderResourceBinding& binding : program.ResourceBindings)
                {
                    bool isUsed = false;
                    metadata->isParameterLocationUsed(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT, binding.Set, binding.Binding, isUsed);
                    if (isUsed) binding.StageMask |= ShaderStageBit(entryPoint.Stage);
                }
            }
        }

        // A module whose push constants are a [Pass] struct (eos.pass) is a render graph pass.
        if (pushConstantStruct && pushConstantStruct->getType() && pushConstantStruct->getType()->findUserAttributeByName("Pass"))
        {
            if (!ReflectPass(pushConstantStruct, layout, program, outDiagnostics)) success = false;
        }

        return success;
    }
}
#endif
