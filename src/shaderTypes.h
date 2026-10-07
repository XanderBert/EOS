#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "enums.h"

// description of shader programs and their reflection.
namespace EOS
{
    /**
     * @brief A preprocessor define passed to the shader compiler.
     */
    struct ShaderMacro final
    {
        std::string Name;
        std::string Value;

        bool operator==(const ShaderMacro&) const = default;
    };

    /**
     * @brief A Slang module given as source text instead of a file, such as one a shader graph generated.
     */
    struct ShaderSourceModule final
    {
        std::string Name;       // as used by `import`, and shown in diagnostics
        std::string Source;

        bool operator==(const ShaderSourceModule&) const = default;
    };

    /**
     * @brief Identifies one compiled shader program: a Slang module plus the entry points and defines it is compiled with.
     */
    struct ShaderProgramDescription final
    {
        // Slang module name, as used by `import`. "shade" loads shade.slang, "eos.imgui" loads eos/imgui.slang.
        // It may also name one of SourceModules.
        std::string Module;

        // Entry points to compile. Empty compiles every entry point the module marks with [shader("...")].
        std::vector<std::string> EntryPoints{};

        std::vector<ShaderMacro> Defines{};

        // More modules linked into the program, from files (by import name) and from memory. This is how a program is
        // specialized at link time: Module declares `extern struct Material : IMaterial;` and a linked module defines it
        // with `export struct Material : IMaterial = MyMaterial;`. One pass serves every material, and only the small
        // module that defines the material is compiled per program.
        std::vector<std::string> LinkModules{};
        std::vector<ShaderSourceModule> SourceModules{};

        bool operator==(const ShaderProgramDescription&) const = default;
    };

    /**
     * @brief Scalar type of reflected shader value.
     */
    enum class ShaderScalarType : uint8_t
    {
        Unknown = 0,
        Boolean,                // not "Bool": X11 headers #define Bool
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float16,
        Float32,
        Float64,
    };

    /**
     * @brief A stage input or output that lives at a location (vertex attribute, interpolant, color target).
     */
    struct ShaderVarying final
    {
        std::string Name;
        std::string Semantic;                   // upper-cased by Slang, e.g. "POSITION", "TEXCOORD", "SV_TARGET"
        uint32_t SemanticIndex = 0;
        uint32_t Location = 0;
        ShaderScalarType ComponentType = ShaderScalarType::Unknown;
        uint8_t ComponentCount = 0;
    };

    /**
     * @brief A [[vk::constant_id(N)]] specialization constant declared by the program.
     */
    struct ShaderSpecializationConstant final
    {
        std::string Name;
        uint32_t ConstantID = 0;
        ShaderScalarType Type = ShaderScalarType::Unknown;
    };

    /**
     * @brief The descriptor type behind a reflected resource binding.
     */
    enum class ShaderResourceType : uint8_t
    {
        Unknown = 0,
        Sampler,
        SampledTexture,
        StorageTexture,
        CombinedTextureSampler,
        UniformBuffer,
        StorageBuffer,
        AccelerationStructure,
        Bindless,               // an untyped bindless array (eos.bindless); the descriptor type depends on how it is used
    };

    /**
     * @brief A descriptor binding declared at global scope.
     */
    struct ShaderResourceBinding final
    {
        std::string Name;
        uint32_t Set = 0;
        uint32_t Binding = 0;
        ShaderResourceType Type = ShaderResourceType::Unknown;
        uint32_t StageMask = 0;                 // bit (1 << ShaderStage) for every entry point that accesses the binding
    };

    /**
     * @brief One compiled entry point: its SPIR-V and everything the pipeline needs to know about it.
     */
    struct ShaderEntryPoint final
    {
        std::string Name;                       // also the SPIR-V OpEntryPoint name
        ShaderStage Stage = ShaderStage::None;
        std::array<uint32_t, 3> ThreadGroupSize{};  // compute, mesh and task stages only
        uint32_t PushConstantSize = 0;          // bytes of push-constant data this entry point reads
        std::vector<ShaderVarying> Inputs{};    // vertex attributes for the vertex stage, interpolants otherwise
        std::vector<ShaderVarying> Outputs{};   // color targets for the fragment stage, interpolants otherwise
        std::vector<uint32_t> Spirv{};
    };

    /**
     * @brief A source file a program was compiled from, used to detect stale caches and drive hot reload.
     */
    struct ShaderSourceDependency final
    {
        std::filesystem::path Path;
        uint64_t ContentHash = 0;
    };

    /**
     * @brief What a field of a Slang pass (eos.pass) is: a pin, a value the engine fills, or a property.
     */
    enum class ShaderPassFieldKind : uint8_t
    {
        // Pins.
        Texture,                // DescriptorHandle<Texture*>
        StorageTexture,         // DescriptorHandle<RWTexture*>
        Buffer,                 // a pointer
        Scene,                  // a pointer to eos.scene's Scene: the scene a gltfScene node loads
        DepthTarget,            // eos.pass's DepthTarget: the depth attachment, no push-constant space
        ColorTarget,            // a field of the fragment shader's output

        // Filled by the engine.
        Sampler,                // DescriptorHandle<SamplerState>: a sampler of [Sampler]'s description

        // Properties.
        Bool,
        Int,
        UInt,
        Float,
        Float2,
        Float3,
        Float4,
        Enum,
    };

    enum class ShaderPassDirection : uint8_t
    {
        None,                   // not a pin
        Input,
        Output,
        InOut,
    };

    /**
     * @brief A field of the [Pass] struct of a Slang pass, or a color target of its fragment shader, with its attributes.
     */
    struct ShaderPassField final
    {
        std::string Name;
        ShaderPassFieldKind Kind = ShaderPassFieldKind::Float;
        ShaderPassDirection Direction = ShaderPassDirection::None;
        uint32_t Offset = 0;                    // in the push constants; a color target's location
        bool Optional = false;

        // Outputs.
        std::string Format;                     // a name of formatNames.h; empty for the swapchain's format
        float Scale = 1.0f;
        uint32_t Width = 0;                     // [Size]: a fixed size instead of Scale; 0 when none
        uint32_t Height = 0;
        uint32_t Layers = 1;                    // [Layers]: an array texture, rendered into a layer per triangle
        std::string SizeOf;
        std::string Bypass;
        std::array<float, 4> Clear{0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t BufferSize = 0;                // pointer outputs: the size of what it points to
        std::string BufferType;                 // pointers: the name of the type they point to ("Scene", "View")

        // Depth pins (a DepthTarget, or a texture input marked [DepthTest]): an EOS::CompareOp, -1 for the default.
        int8_t DepthCompare = -1;

        // Samplers ([Sampler]): an EOS::SamplerFilter (min, mag and mip) and an EOS::SamplerWrap.
        uint8_t SamplerFilter = 1;
        uint8_t SamplerAddress = 0;

        // Properties.
        float Minimum = 0.0f;                   // [Range]; none when equal
        float Maximum = 0.0f;
        double Default = 0.0;                   // scalars
        std::vector<std::string> EnumNames{};   // enums, with the value of each case
        std::vector<int64_t> EnumValues{};

        bool operator==(const ShaderPassField&) const = default;
    };

    /**
     * @brief A fragment entry point of a raster pass, and the materials it draws ([DrawScene] passes, eos.scene).
     */
    struct ShaderPassFragment final
    {
        std::string EntryPoint;
        uint8_t Materials = 0x7;                // bit (1 << AlphaMode): the alpha modes of the instances it draws

        bool operator==(const ShaderPassFragment&) const = default;
    };

    /**
     * @brief The render graph pass a Slang module declares (eos.pass), when its push constants are a [Pass] struct.
     */
    struct ShaderPassReflection final
    {
        bool IsPass = false;
        std::vector<ShaderPassField> Fields{};  // push-constant fields in offset order, then color targets by location

        // Compute passes: [DispatchThreads], or [DispatchSizeOf] a texture pin, else the first storage texture output.
        std::array<uint32_t, 3> DispatchThreads{0, 0, 0};
        std::string DispatchSizeOf;

        // Raster passes.
        std::vector<ShaderPassFragment> Fragments{};
        bool DrawScene = false;                 // [DrawScene]: draws the scene on its Scene pin
        uint8_t Cull = 0;                       // [Cull]: an EOS::CullMode
        bool DepthClamp = false;                // [DepthClamp]

        bool operator==(const ShaderPassReflection&) const = default;
    };

    /**
     * @brief A fully compiled shader program with its reflection.
     */
    struct CompiledShaderProgram final
    {
        ShaderProgramDescription Description;
        std::vector<ShaderEntryPoint> EntryPoints{};
        std::vector<ShaderSpecializationConstant> SpecializationConstants{};
        std::vector<ShaderResourceBinding> ResourceBindings{};
        std::vector<ShaderSourceDependency> Dependencies{};
        std::string CompilerVersion{};          // Slang build tag the program was compiled with
        uint32_t PushConstantSize = 0;          // largest push-constant block of any entry point
        ShaderPassReflection Pass{};            // the render graph pass the module declares, if any

        [[nodiscard]] const ShaderEntryPoint* FindEntryPoint(std::string_view name) const
        {
            for (const ShaderEntryPoint& entryPoint : EntryPoints)
            {
                if (entryPoint.Name == name) return &entryPoint;
            }

            return nullptr;
        }

        [[nodiscard]] const ShaderSpecializationConstant* FindSpecializationConstant(std::string_view name) const
        {
            for (const ShaderSpecializationConstant& constant : SpecializationConstants)
            {
                if (constant.Name == name) return &constant;
            }

            return nullptr;
        }
    };

    [[nodiscard]] constexpr uint32_t ShaderStageBit(ShaderStage stage)
    {
        return 1u << static_cast<uint32_t>(stage);
    }

    [[nodiscard]] constexpr const char* ToString(ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::Vertex:        return "Vertex";
            case ShaderStage::Hull:          return "Hull";
            case ShaderStage::Domain:        return "Domain";
            case ShaderStage::Geometry:      return "Geometry";
            case ShaderStage::Fragment:      return "Fragment";
            case ShaderStage::Compute:       return "Compute";
            case ShaderStage::RayGen:        return "RayGen";
            case ShaderStage::Intersection:  return "Intersection";
            case ShaderStage::AnyHit:        return "AnyHit";
            case ShaderStage::ClosestHit:    return "ClosestHit";
            case ShaderStage::Miss:          return "Miss";
            case ShaderStage::Callable:      return "Callable";
            case ShaderStage::Mesh:          return "Mesh";
            case ShaderStage::Amplification: return "Amplification";
            case ShaderStage::None:          return "None";
        }

        return "None";
    }
}
