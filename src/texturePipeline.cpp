#include "texturePipeline.h"

#include <vector>
#include <ktx.h>
#include <vulkan/vk_enum_string_helper.h>

#include "EOS.h"
#include "logger.h"

#if defined(EOS_BUILD_TEXTURE_TOOLS)
#include "TextureTools/texture_packer.h"
#endif

namespace EOS::TexturePipeline
{
    EOS::Format KtxVkFormatToEOSFormat(const uint32_t vkFormat)
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

    EOS::Holder<EOS::TextureHandle> LoadTexture(const EOS::TextureLoadingDescription& textureLoadingDescription)
    {
        CHECK(!textureLoadingDescription.InputFilePath.empty() || std::filesystem::exists(textureLoadingDescription.InputFilePath), "{} : Is not a valid Texture Path.", textureLoadingDescription.InputFilePath.string());
        CHECK(std::filesystem::is_regular_file(textureLoadingDescription.InputFilePath), "{} : Is not a valid Texture file.", textureLoadingDescription.InputFilePath.string());
        CHECK(!textureLoadingDescription.OutputFilePath.empty(), "Please Specify a output path for the texture compression");
        CHECK(std::filesystem::is_directory(textureLoadingDescription.OutputFilePath), "{} : Is not a valid compression directory", textureLoadingDescription.OutputFilePath.string());

        ktxTexture2* texture = nullptr;
        const std::filesystem::path compressedPath = textureLoadingDescription.OutputFilePath / textureLoadingDescription.InputFilePath.filename().replace_extension("ktx2");
        if (!std::filesystem::exists(compressedPath))
        {
#if defined(EOS_BUILD_TEXTURE_TOOLS)
            CHECK(EOS::TexturePacking::CompressTextureToCache(
                    textureLoadingDescription.InputFilePath,
                    compressedPath,
                    textureLoadingDescription.TextureCompression),
                "Failed to build compressed texture cache '{}' from source '{}'",
                compressedPath.string(),
                textureLoadingDescription.InputFilePath.string());
#else
            CHECK(false,
                "Missing compressed texture '{}'. Build with EOS_BUILD_TEXTURE_TOOLS=ON or precompress textures before shipping.",
                compressedPath.string());
#endif
        }

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
        TextureHolder loadedTexture = textureLoadingDescription.Context->CreateTexture(
        {
            .Type                   = textureLoadingDescription.Type,
            .TextureFormat          = textureFormat,
            .TextureDimensions      = {texture->baseWidth, texture->baseHeight},
            .NumberOfMipLevels      = texture->numLevels,
            .Usage                  = textureLoadingDescription.Usage,
            .Data                   = packedData.data(),
            .DataNumberOfMipLevels  = texture->numLevels,
            .DebugName              = compressedPath.string().c_str(),
        });

        ktxTexture_Destroy(ktxTexture(texture));

        return std::move(loadedTexture);
    }
}