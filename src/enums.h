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
}
