#include "texture_packer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

#include "ktx.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize2.h>

namespace
{
    struct CompressionInfo final
    {
        ktx_transcode_fmt_e transcodeFormat = KTX_TTF_NOSELECTION;
        bool useUASTC = false;
        bool normalMap = false;
    };

    [[nodiscard]] uint32_t CalculateNumberOfMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels)
        {
            ++levels;
        }

        return levels;
    }

    [[nodiscard]] CompressionInfo ResolveCompressionInfo(const EOS::Compression compression)
    {
        switch (compression)
        {
            case EOS::Compression::ETC2: return { KTX_TTF_ETC2_RGBA, false, false };
            case EOS::Compression::BC5: return { KTX_TTF_BC5_RG, true, true };
            case EOS::Compression::BC7: return { KTX_TTF_BC7_RGBA, false, false };
            case EOS::Compression::NoCompression:
            default: return { KTX_TTF_NOSELECTION, false, false };
        }
    }

    [[nodiscard]] ktxTexture2* CompressTexture(const uint8_t* pixels, const int width, const int height, const EOS::Compression compression)
    {
        const CompressionInfo info = ResolveCompressionInfo(compression);
        const uint32_t mipLevels = CalculateNumberOfMipLevels(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

        const ktxTextureCreateInfo createInfo
        {
            .glInternalformat = 0x8058,
            .vkFormat         = static_cast<uint32_t>(43), //VK_FORMAT_R8G8B8A8_SRGB = 43,
            .baseWidth        = static_cast<uint32_t>(width),
            .baseHeight       = static_cast<uint32_t>(height),
            .baseDepth        = 1u,
            .numDimensions    = 2u,
            .numLevels        = mipLevels,
            .numLayers        = 1u,
            .numFaces         = 1u,
            .generateMipmaps  = KTX_FALSE,
        };

        ktxTexture2* texture = nullptr;
        if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        {
            return nullptr;
        }

        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            size_t offset = 0;
            ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset);
            uint8_t* destination = ktxTexture_GetData(ktxTexture(texture)) + offset;

            const int mipWidth = std::max(1, width >> static_cast<int>(level));
            const int mipHeight = std::max(1, height >> static_cast<int>(level));

            if (level == 0)
            {
                const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
                std::memcpy(destination, pixels, bytes);
            }
            else
            {
                stbir_resize_uint8_linear(
                    pixels, width, height, 0,
                    destination, mipWidth, mipHeight, 0,
                    STBIR_RGBA);
            }
        }

        if (info.transcodeFormat != KTX_TTF_NOSELECTION)
        {
            ktxBasisParams basisParams = {};
            basisParams.structSize = sizeof(basisParams);
            basisParams.noSSE = KTX_FALSE;
            basisParams.threadCount = std::max(1u, std::thread::hardware_concurrency());
            basisParams.uastc = info.useUASTC ? KTX_TRUE : KTX_FALSE;
            basisParams.uastcFlags = info.useUASTC ? KTX_PACK_UASTC_LEVEL_DEFAULT : 0;
            basisParams.normalMap = info.normalMap ? KTX_TRUE : KTX_FALSE;

            if (ktxTexture2_CompressBasisEx(texture, &basisParams) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(texture));
                return nullptr;
            }

            if (ktxTexture2_TranscodeBasis(texture, info.transcodeFormat, 0) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(texture));
                return nullptr;
            }
        }

        return texture;
    }
}

namespace EOS::TexturePacking
{
    bool CompressTextureToCache(const std::filesystem::path& sourcePath, const std::filesystem::path& outputPath, const Compression compression)
    {
        std::cout << "[info] Compressing texture: " << sourcePath.string().c_str() << std::endl;
        int width = 0;
        int height = 0;
        int channels = 0;
        constexpr int desiredChannels = 4;
        uint8_t* pixels = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, desiredChannels);
        if (pixels == nullptr)
        {
            std::cerr << "[error] failed to load " << sourcePath << std::endl;
            return false;
        }

        ktxTexture2* texture = CompressTexture(pixels, width, height, compression);
        stbi_image_free(pixels);

        if (texture == nullptr)
        {
            std::cerr << "[error] failed to compress " << sourcePath << std::endl;
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(outputPath.parent_path(), errorCode);
        if (errorCode)
        {
            std::cerr << "[error] failed to create directories for " << outputPath << std::endl;
            ktxTexture_Destroy(ktxTexture(texture));
            return false;
        }

        if (ktxTexture_WriteToNamedFile(ktxTexture(texture), outputPath.string().c_str()) != KTX_SUCCESS)
        {
            std::cerr << "[error] failed to write " << outputPath << std::endl;
            ktxTexture_Destroy(ktxTexture(texture));
            return false;
        }

        ktxTexture_Destroy(ktxTexture(texture));
        return true;
    }
}