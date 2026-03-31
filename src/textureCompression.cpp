#include "textureCompression.h"

#include <cstring>

#include "stb_image_resize2.h"
#include "utils.h"

ktxTexture2* TextureCompressor::CompressTexture(uint8_t* pixels, int width, int height, EOS::Compression compression)
{
    const CompressionInfo     info             = ResolveCompressionInfo(compression);
    const uint32_t            numberOfMipLevels = EOS::CalculateNumberOfMipLevels(width, height);

    const ktxTextureCreateInfo createInfo
    {
        .glInternalformat = 0x8058, // GL_RGBA8
        .vkFormat         = static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_SRGB),
        .baseWidth        = static_cast<uint32_t>(width),
        .baseHeight       = static_cast<uint32_t>(height),
        .baseDepth        = 1u,
        .numDimensions    = 2u,
        .numLevels        = numberOfMipLevels,
        .numLayers        = 1u,
        .numFaces         = 1u,
        .generateMipmaps  = KTX_FALSE,
    };

    ktxTexture2* texture = nullptr;
    CHECK(ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS, "Could not create KTX2 texture");

    int w = width, h = height;
    for (uint32_t i = 0; i < numberOfMipLevels; ++i)
    {
        size_t offset = 0;
        ktxTexture_GetImageOffset(ktxTexture(texture), i, 0, 0, &offset);
        uint8_t* dst = ktxTexture_GetData(ktxTexture(texture)) + offset;

        if (i == 0)
        {
            const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
            std::memcpy(dst, pixels, bytes);
        }
        else
        {
            stbir_resize_uint8_linear(
                static_cast<const unsigned char*>(pixels), width, height, 0,
                dst, w, h, 0,
                STBIR_RGBA);
        }

        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
    }

    if (info.transcodeFormat != KTX_TTF_NOSELECTION)
    {
        ktxBasisParams basisParams      = {};
        basisParams.structSize          = sizeof(basisParams);
        basisParams.noSSE               = KTX_FALSE;
        basisParams.uastc               = info.useUASTC  ? KTX_TRUE : KTX_FALSE;
        basisParams.uastcFlags          = info.useUASTC  ? KTX_PACK_UASTC_LEVEL_DEFAULT : 0;
        basisParams.normalMap           = info.normalMap ? KTX_TRUE : KTX_FALSE;

        CHECK(ktxTexture2_CompressBasisEx(texture, &basisParams) == KTX_SUCCESS, "Could not compress image to Basis");
        CHECK(ktxTexture2_TranscodeBasis(texture, info.transcodeFormat, 0) == KTX_SUCCESS, "Could not transcode image");
    }

    return texture;
}

EOS::Format TextureCompressor::CompressionToFormat(const EOS::Compression compression)
{
    switch (compression)
    {
        case EOS::BC5:           return EOS::Format::BC5_RG;
        case EOS::BC7:           return EOS::Format::BC7_RGBA;
        case EOS::ETC2:          return EOS::Format::ETC2_SRGB8;
        case EOS::NoCompression: return EOS::Format::RGBA_SRGB8;
        default:                 return EOS::Format::RGBA_SRGB8;
    }
}

TextureCompressor::CompressionInfo TextureCompressor::ResolveCompressionInfo(EOS::Compression compression)
{
    switch (compression)
    {
    case EOS::ETC2:          return { KTX_TTF_ETC2_RGBA, false, false };
    case EOS::BC5:           return { KTX_TTF_BC5_RG,    true,  true  };
    case EOS::BC7:           return { KTX_TTF_BC7_RGBA,  false, false };
    case EOS::NoCompression:
    default:                 return { KTX_TTF_NOSELECTION, false, false };
    }
}
