#include "utils.h"
#include <fstream>
#include <vector>
#include <ios>
#include <limits>

#include "logger.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize2.h>

#include <ktx.h>
#include "EOS.h"
#include "vulkan/vkTools.h"

namespace EOS
{
    std::string ReadFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);

        // Check if the file opened successfully
        if (!file.is_open())
        {
            EOS::Logger->error("I/O error. Cannot open file '{}'", filePath.string());
            return std::string();
        }

        // Read the entire file content into a string.
        std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());

        file.close();

        // Check if a BOM character exists. If so, replace it with a regular space.
        // BOM (UTF-8): EF BB BF
        static constexpr unsigned char BOM[] = { 0xEF, 0xBB, 0xBF };
        if (content.size() >= 3)
        {
            // Compare the first 3 bytes of the string with the BOM
            if (memcmp(content.data(), BOM, 3) == 0)
            {
                // Replace the BOM characters with spaces
                content[0] = ' ';
                content[1] = ' ';
                content[2] = ' ';
            }
        }

        return content;
    }

    void WriteFile(const std::filesystem::path& filePath, const std::string& content)
    {
        std::ofstream out( filePath, std::ios::out | std::ios::binary);
        if (!out.is_open())
        {
            EOS::Logger->error("I/O error. Cannot Open File '{}'", filePath.string());
        }

        out.write(content.data(), content.size());
        out.close();
    }

    uint32_t GetVertexFormatSize(const EOS::VertexFormat format)
    {

        #define SIZE4(EOSBaseType, BaseType)                                \
        case VertexFormat::EOSBaseType##1: return sizeof(BaseType) * 1u;    \
        case VertexFormat::EOSBaseType##2: return sizeof(BaseType) * 2u;    \
        case VertexFormat::EOSBaseType##3: return sizeof(BaseType) * 3u;    \
        case VertexFormat::EOSBaseType##4: return sizeof(BaseType) * 4u;

        #define SIZE2_4_NORM(EOSBaseType, BaseType)                             \
        case VertexFormat::EOSBaseType##2Norm: return sizeof(BaseType) * 2u;    \
        case VertexFormat::EOSBaseType##4Norm: return sizeof(BaseType) * 4u;

        switch (format)
        {
            SIZE4(Float, float);
            SIZE4(Byte, uint8_t);
            SIZE4(UByte, uint8_t);
            SIZE4(Short, uint16_t);
            SIZE4(UShort, uint16_t);
            SIZE2_4_NORM(Byte, uint8_t);
            SIZE2_4_NORM(UByte, uint8_t);
            SIZE2_4_NORM(Short, uint16_t);
            SIZE2_4_NORM(UShort, uint16_t);
            SIZE4(Int, uint32_t);
            SIZE4(UInt, uint32_t);
            SIZE4(HalfFloat, uint16_t);
            default:
                assert(false);
                return 0;
        }

#undef SIZE4
#undef SIZE2_4_NORM

    }

    uint32_t GetSizeAligned(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    EOS::Holder<EOS::TextureHandle> LoadTexture(const TextureLoadingDescription& textureLoadingDescription)
    {
        ktxTexture1* texture = nullptr;

        uint8_t* pixels = nullptr;
        int originalWidth = 0;
        int originalHeight = 0;

        //Check if texture is already stored in the cache if so load in the ktx instead of the other file.
        const std::filesystem::path cachedFilePath = fmt::format(".cache/{}", textureLoadingDescription.filePath.filename().replace_extension(".ktx").string());
        std::ifstream file(cachedFilePath, std::ios::in | std::ios::binary);
        if (file.is_open())
        {
            file.close();
            EOS::Logger->debug("{} was already compressed and cached", textureLoadingDescription.filePath.filename().string());
            CHECK(ktxTexture1_CreateFromNamedFile(cachedFilePath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture) == KTX_SUCCESS, "Could not load cached KTX texture: {}", cachedFilePath.string().c_str());
        }
        else
        {
            constexpr int desiredChannels = 4;
            int channels;
            pixels = stbi_load(textureLoadingDescription.filePath.string().c_str(), &originalWidth, &originalHeight, &channels, desiredChannels);
            CHECK(pixels, "Could not load image at location: {}", textureLoadingDescription.filePath.string().c_str());
            EOS::Logger->debug({"Loading image: {} | Size: {}x{} | Channels: {}"}, textureLoadingDescription.filePath.filename().string(), originalWidth, originalHeight, channels);

            texture = CompressTexture(pixels, originalWidth, originalHeight, textureLoadingDescription.compression);
            ktxTexture_WriteToNamedFile(ktxTexture(texture), cachedFilePath.string().c_str());
            stbi_image_free(pixels);
        }

        
        Holder<EOS::TextureHandle> loadedTexture = textureLoadingDescription.context->CreateTexture(
        {
            .Type                   = EOS::ImageType::Image_2D,
            .TextureFormat          = EOS::Format::RGBA_UN8,
            .TextureDimensions      = {texture->baseWidth, texture->baseHeight},
            .NumberOfMipLevels      = texture->numLevels,
            .Usage                  = EOS::TextureUsageFlags::Sampled,
            .Data                   = texture->pData,
            .DataNumberOfMipLevels  = texture->numLevels,
            .DebugName              = cachedFilePath.string().c_str(),
        });

        
        ktxTexture_Destroy(ktxTexture(texture));
        return loadedTexture;
    }

    ktxTexture1* CompressTexture(uint8_t* pixels, int width, int height, Compression compression)
    {
        VkFormat desiredFormat{VK_FORMAT_R8G8B8A8_SRGB}; // Initial format
        uint32_t glInternalFormat = 0x8058; // GL_RGBA8 - default
        const uint32_t numberOfMipLevels = CalculateNumberOfMipLevels(width, height);

        // create a KTX2 texture for RGBA data
        ktxTextureCreateInfo createInfoKTX2
        {
            .glInternalformat = 0x8058, // = GL_RGBA8 -> i prefer no dependancy on GL
            .vkFormat         = static_cast<uint32_t>(desiredFormat),
            .baseWidth        = static_cast<uint32_t>(width),
            .baseHeight       = static_cast<uint32_t>(height),
            .baseDepth        = 1u,
            .numDimensions    = 2u,
            .numLevels        = numberOfMipLevels,
            .numLayers        = 1u,
            .numFaces         = 1u,
            .generateMipmaps  = KTX_FALSE,
        };

        ktxTexture2* textureKTX2 = nullptr;
        CHECK(ktxTexture2_Create(&createInfoKTX2, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &textureKTX2) == KTX_SUCCESS, "Could not create a KTX texture from the given pixel data for image");

        int w = width;
        int h = height;

        // generate mip-map
        for (uint32_t i = 0; i != numberOfMipLevels; ++i)
        {
            size_t offset = 0;
            ktxTexture_GetImageOffset(ktxTexture(textureKTX2), i, 0, 0, &offset);
            stbir_resize_uint8_linear(static_cast<const unsigned char *>(pixels), width, height, 0, ktxTexture_GetData(ktxTexture(textureKTX2)) + offset, w, h, 0, STBIR_RGBA);

            h = h > 1 ? h >> 1 : 1;
            w = w > 1 ? w >> 1 : 1;
        }

        ktx_transcode_fmt_e compressionMethod;
        switch (compression)
        {
            case ETC1:
                compressionMethod = KTX_TTF_ETC1_RGB;
                desiredFormat = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK; // ETC1 uses ETC2 format
                glInternalFormat = 0x8D64; // GL_COMPRESSED_RGB8_ETC2
                break;
            case ETC2:
                compressionMethod = KTX_TTF_ETC2_RGBA;
                desiredFormat = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK; // Fixed to include alpha
                glInternalFormat = 0x8D68; // GL_COMPRESSED_RGBA8_ETC2_EAC
                break;
            case BC1:
                compressionMethod = KTX_TTF_BC1_RGB;
                desiredFormat = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                glInternalFormat = 0x83F1; // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                break;
            case BC3:
                compressionMethod = KTX_TTF_BC3_RGBA;
                desiredFormat = VK_FORMAT_BC3_UNORM_BLOCK;
                glInternalFormat = 0x83F3; // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
                break;
            case BC4:
                compressionMethod = KTX_TTF_BC4_R;
                desiredFormat = VK_FORMAT_BC4_UNORM_BLOCK;
                glInternalFormat = 0x8DBB; // GL_COMPRESSED_RED_RGTC1
                break;
            case BC5:
                compressionMethod = KTX_TTF_BC5_RG;
                desiredFormat = VK_FORMAT_BC5_UNORM_BLOCK;
                glInternalFormat = 0x8DBD; // GL_COMPRESSED_RG_RGTC2
                break;
            case BC7:
                compressionMethod = KTX_TTF_BC7_RGBA;
                desiredFormat = VK_FORMAT_BC7_UNORM_BLOCK;
                glInternalFormat = 0x8E8C; // GL_COMPRESSED_RGBA_BPTC_UNORM
                break;
            case ASTC:
                compressionMethod = KTX_TTF_ASTC_4x4_RGBA;
                desiredFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
                glInternalFormat = 0x93B0; // GL_COMPRESSED_RGBA_ASTC_4x4_KHR
                break;
            case NoCompression:
            default:
                compressionMethod = KTX_TTF_NOSELECTION;
                desiredFormat = VK_FORMAT_R8G8B8A8_SRGB;
                glInternalFormat = 0x8058; // GL_RGBA8
                break;
        }

        if (compressionMethod != KTX_TTF_NOSELECTION)
        {
            // compress to Basis and transcode
            CHECK(ktxTexture2_CompressBasis(textureKTX2, 255) == KTX_SUCCESS, "Could not compress the image to the base compression");
            CHECK(ktxTexture2_TranscodeBasis(textureKTX2, compressionMethod, 0) == KTX_SUCCESS, "Could not compress Image");
        }

        // convert to KTX1 - now using the correct format
        const ktxTextureCreateInfo createInfoKTX1
        {
            .glInternalformat = glInternalFormat, // Now matches the compression format
            .vkFormat         = static_cast<uint32_t>(desiredFormat),
            .baseWidth        = static_cast<uint32_t>(width),
            .baseHeight       = static_cast<uint32_t>(height),
            .baseDepth        = 1u,
            .numDimensions    = 2u,
            .numLevels        = numberOfMipLevels,
            .numLayers        = 1u,
            .numFaces         = 1u,
            .generateMipmaps  = KTX_FALSE,
        };

        ktxTexture1* textureKTX1 = nullptr;
        CHECK(ktxTexture1_Create(&createInfoKTX1, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &textureKTX1) == KTX_SUCCESS, "Could not create KTX1 Texture from");

        for (uint32_t i = 0; i != numberOfMipLevels; ++i)
        {
            size_t offset1 = 0;
            size_t offset2 = 0;

            CHECK(ktxTexture_GetImageOffset(ktxTexture(textureKTX1), i, 0, 0, &offset1) == KTX_SUCCESS, "Error getting image offset while generating mip maps");
            CHECK(ktxTexture_GetImageOffset(ktxTexture(textureKTX2), i, 0, 0, &offset2) == KTX_SUCCESS, "Error getting image offset while generating mip maps");

            size_t imageSize = ktxTexture_GetImageSize(ktxTexture(textureKTX1), i);
            memcpy(ktxTexture_GetData(ktxTexture(textureKTX1)) + offset1, ktxTexture_GetData(ktxTexture(textureKTX2)) + offset2, imageSize);
        }
        ktxTexture_Destroy(ktxTexture(textureKTX2));
        return textureKTX1;
    }
}


