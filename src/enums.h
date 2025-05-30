#pragma once

namespace EOS
{
    enum class HardwareDeviceType : uint8_t
    {
        Integrated  = 1,
        Discrete    = 2,
        Virtual     = 3,
        Software    = 4
    };

    enum class ColorSpace : uint8_t
    {
        SRGB_Linear,
        SRGB_NonLinear
    };

    enum class ImageType : uint8_t
    {
        Image_1D        = 0,
        Image_2D        = 1,
        Image_3D        = 2,
        CubeMap         = 3,
        Image_1D_Array  = 4,
        Image_2D_Array  = 5,
        CubeMap_Array   = 6,
        SwapChain       = 7,
    };

    enum ResourceState : uint32_t
    {
        Undefined = 0,

        // Input Assembly (Vertex/Index)
        VertexAndConstantBuffer = 0x00000001,
        VertexBuffer            = 0x00000002,
        IndexBuffer             = 0x00000003,

        // Render Targets
        RenderTarget          = 0x00000004,
        UnorderedAccess       = 0x00000008,

        // Depth/Stencil
        DepthWrite            = 0x00000010,
        DepthRead             = 0x00000020,

        // Shader Resources
        NonPixelShaderResource = 0x00000040,  // VS/CS/GS/DS
        PixelShaderResource    = 0x00000080,  // PS only
        ShaderResource         = NonPixelShaderResource | PixelShaderResource,

        // Copy Operations
        CopyDest              = 0x00000100,
        CopySource            = 0x00000200,

        // Indirect/Streamout
        IndirectArgument      = 0x00000400,
        StreamOut             = 0x00000800,

        // Presentation
        Present               = 0x00001000,

        // Common/Generic Read
        Common                = 0x00002000,
        GenericRead           = VertexBuffer | IndexBuffer |
                               NonPixelShaderResource | PixelShaderResource |
                               IndirectArgument | CopySource,

        // Raytracing
        AccelerationStructureRead  = 0x00004000,
        AccelerationStructureWrite = 0x00008000,

        // Special Cases
        UnorderedAccessPixel       = 0x00010000  // UAV in pixel shader
    };

    enum class ShaderStage : uint8_t
    {
        Vertex = 0,
        Hull,
        Domain,
        Geometry,
        Fragment,
        Compute,
        RayGen,
        Intersection,
        AnyHit,
        ClosestHit,
        Miss,
        Callable,
        Mesh,
        Amplification,
        None,
    };

    enum class VertexFormat : uint8_t
    {
        Invalid = 0,

        Float1,
        Float2,
        Float3,
        Float4,

        Byte1,
        Byte2,
        Byte3,
        Byte4,

        UByte1,
        UByte2,
        UByte3,
        UByte4,

        Short1,
        Short2,
        Short3,
        Short4,

        UShort1,
        UShort2,
        UShort3,
        UShort4,

        Byte2Norm,
        Byte4Norm,

        UByte2Norm,
        UByte4Norm,

        Short2Norm,
        Short4Norm,

        UShort2Norm,
        UShort4Norm,

        Int1,
        Int2,
        Int3,
        Int4,

        UInt1,
        UInt2,
        UInt3,
        UInt4,

        HalfFloat1,
        HalfFloat2,
        HalfFloat3,
        HalfFloat4,
    };

    enum class Topology : uint8_t
    {
        Point,
        Line,
        LineStrip,
        Triangle,
        TriangleStrip,
        Patch,
    };

    enum class CullMode : uint8_t
    {
        None,
        Front,
        Back,
    };

    enum class WindingMode : uint8_t
    {
        CounterClockWise,
        ClockWise
    };

    enum class Format : uint8_t
    {
        Invalid = 0,

        R_UN8,
        R_UI16,
        R_UI32,
        R_UN16,
        R_F16,
        R_F32,

        RG_UN8,
        RG_UI16,
        RG_UI32,
        RG_UN16,
        RG_F16,
        RG_F32,

        RGBA_UN8,
        RGBA_UI32,
        RGBA_F16,
        RGBA_F32,
        RGBA_SRGB8,
        BGRA_UN8,

        BGRA_SRGB8,

        ETC2_RGB8,
        ETC2_SRGB8,

        BC7_RGBA,

        Z_UN16,
        Z_UN24,
        Z_F32,
        Z_UN24_S_UI8,
        Z_F32_S_UI8,

        YUV_NV12,
        YUV_420p,
    };


    enum class BlendOp : uint8_t
    {
        Add = 0,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class BlendFactor : uint8_t
    {
        Zero = 0,
        One,
        SrcColor,
        OneMinusSrcColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstColor,
        OneMinusDstColor,
        DstAlpha,
        OneMinusDstAlpha,
        SrcAlphaSaturated,
        BlendColor,
        OneMinusBlendColor,
        BlendAlpha,
        OneMinusBlendAlpha,
        Src1Color,
        OneMinusSrc1Color,
        Src1Alpha,
        OneMinusSrc1Alpha
    };

    enum class PolygonMode : uint8_t
    {
        Fill,
        Line
    };

    enum class StencilOp : uint8_t
    {
        Keep = 0,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap
    };

    enum class CompareOp : uint8_t
    {
        Never = 0,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        AlwaysPass
    };

    // These bindings should match SLANG declarations
    //TODO: We can inject these bindings into the shadermodule
    enum Bindings : uint8_t
    {
        Textures = 0,
        Samplers = 1,
        StorageImages = 2,
        AccelerationStructures = 2,
        Count = 4,
      };

    enum class LoadOp : uint8_t
    {
        Invalid = 0,
        DontCare,
        Load,
        Clear,
        None,
    };

    enum class StoreOp : uint8_t
    {
        DontCare = 0,
        Store,
        MsaaResolve,
        None,
    };

    enum class StorageType : uint8_t
    {
        Device,
        HostVisible,
        Memoryless
    };

    enum BufferUsageFlags : uint8_t
    {
        None = 0x00,
        Index = 0x01,
        Vertex = 0x02,
        Uniform = 0x04,
        Storage = 0x08,
        Indirect = 0x10,
        ShaderBindingTable = 0x20,
        AccelStructBuildInputReadOnly = 0x40,
        AccelStructStorage = 0x80
    };
}