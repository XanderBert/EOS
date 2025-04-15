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
        VertexAndConstantBuffer = 0x1,
        IndexBuffer = 0x2,
        RenderTarget = 0x4,
        UnorderedAccess = 0x8,
        DepthWrite = 0x10,
        DepthRead = 0x20,
        NonPixelShaderResource = 0x40,
        PixelShaderResource = 0x80,
        ShaderResource = 0x40 | 0x80,
        StreamOut = 0x100,
        IndirectArgument = 0x200,
        CopyDest = 0x400,
        CopySource = 0x800,
        Read = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
        Present = 0x1000,
        Common = 0x2000,
        AccelerationStructureRead = 0x4000,
        AccelerationStructureWrite =  0x8000,
        UnorderedAccessPixel = 0x10000,
    };
}
