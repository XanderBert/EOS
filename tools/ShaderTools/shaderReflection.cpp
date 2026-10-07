#include "shaderReflection.h"

#if defined(EOS_SHADER_TOOLS)
#include <algorithm>

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
        [[nodiscard]] bool ReflectGlobalScope(slang::ProgramLayout* layout, CompiledShaderProgram& program, uint32_t& outPushConstantSize, std::string& outDiagnostics)
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
        bool success = ReflectGlobalScope(layout, program, globalPushConstantSize, outDiagnostics);

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

        return success;
    }
}
#endif
