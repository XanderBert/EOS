#include "utils.h"
#include <fstream>
#include <vector>

#include <ktx.h>
#include <vulkan/vk_enum_string_helper.h>
#include <volk.h>

#include "EOS.h"
#include "logger.h"
#include "texturePipeline.h"

namespace EOS
{
    [[nodiscard]] static EOS::Format KtxVkFormatToEOSFormat(const uint32_t vkFormat)
    {
        switch (vkFormat)
        {
            case VK_FORMAT_R8G8B8A8_SRGB: return EOS::Format::RGBA_SRGB8;
            case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return EOS::Format::ETC2_SRGB8;
            case VK_FORMAT_BC5_UNORM_BLOCK: return EOS::Format::BC5_RG;
            case VK_FORMAT_BC7_UNORM_BLOCK: return EOS::Format::BC7_RGBA;
            case VK_FORMAT_BC7_SRGB_BLOCK: return EOS::Format::BC7_RGBA;
            default: return EOS::Format::Invalid;
        }
    }

    std::string ReadFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);

        // Check if the file opened successfully
        CHECK(file.is_open(), "I/O error. Cannot open file '{}'", filePath.string());
        if (!file.is_open())
        {
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
        CHECK(out.is_open(), "I/O error. Cannot Open File '{}'", filePath.string());

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

    uint64_t GetAddressAligned(uint64_t address, uint64_t alignment)
    {
        const uint64_t offs = address % alignment;
        return offs ? address + (alignment - offs) : address;
    }

    EOS::Holder<EOS::TextureHandle> LoadTexture(const TextureLoadingDescription& textureLoadingDescription)
    {
        ktxTexture2* texture = nullptr;
        const std::filesystem::path compressedPath = EOS::TexturePipeline::BuildCompressedPathFromSource(textureLoadingDescription.filePath, textureLoadingDescription.compression);

        CHECK(std::filesystem::exists(compressedPath),"Missing compressed texture '{}'. Run the EOSTextureCompressor prebuild step.", compressedPath.string());

        CHECK(ktxTexture2_CreateFromNamedFile(compressedPath.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture) == KTX_SUCCESS, "Could not load compressed KTX texture: {}", compressedPath.string());

          const VkFormat ktxVkFormat = static_cast<VkFormat>(texture->vkFormat);
          const EOS::Format textureFormat = KtxVkFormatToEOSFormat(texture->vkFormat);
          CHECK(textureFormat != EOS::Format::Invalid, "Unsupported KTX vkFormat '{}' ({}) in '{}'", string_VkFormat(ktxVkFormat), texture->vkFormat, compressedPath.string());

        // Pack mip data tightly. KTX levels are 4-byte aligned
        size_t packedSize = 0;
        for (uint32_t level = 0; level < texture->numLevels; ++level)
        {
            packedSize += ktxTexture_GetImageSize(ktxTexture(texture), level);
        }

        std::vector<uint8_t> packedData(packedSize);
        size_t packedOffset = 0;
        for (uint32_t level = 0; level < texture->numLevels; ++level)
        {
            size_t levelOffset = 0;
            ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &levelOffset);
            const size_t levelSize = ktxTexture_GetImageSize(ktxTexture(texture), level);
            std::memcpy(packedData.data() + packedOffset, texture->pData + levelOffset, levelSize);
            packedOffset += levelSize;
        }

        //Upload the texture to the GPU
        Holder<EOS::TextureHandle> loadedTexture = textureLoadingDescription.context->CreateTexture(
        {
            .Type                   = ImageType::Image_2D,
            .TextureFormat          = textureFormat,
            .TextureDimensions      = {texture->baseWidth, texture->baseHeight},
            .NumberOfMipLevels      = texture->numLevels,
            .Usage                  = Sampled,
            .Data                   = packedData.data(),
            .DataNumberOfMipLevels  = texture->numLevels,
            .DebugName              = compressedPath.string().c_str(),
        });

        ktxTexture_Destroy(ktxTexture(texture));

        return std::move(loadedTexture);
    }

    std::filesystem::file_time_type GetLastWriteTime(const std::string& fileName)
    {
        CHECK(!fileName.empty(), "filename is empty");
        if (fileName.empty()) return {};

        const std::filesystem::path path(fileName);

        std::error_code errorCode;
        if (!std::filesystem::exists(path, errorCode) || errorCode) return {};

        return std::filesystem::last_write_time(path, errorCode);
    }
}


