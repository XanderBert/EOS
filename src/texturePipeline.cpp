#include "texturePipeline.h"

#include <vector>
#include <ktx.h>
#include <vulkan/vk_enum_string_helper.h>

#include "EOS.h"
#include "logger.h"

#if defined(EOS_BUILD_TEXTURE_TOOLS)
#include "texture_packer.h"
#endif

namespace EOS::TexturePipeline
{
    const char* GetCompressionSuffix(const Compression compression)
    {
        switch (compression)
        {
        case Compression::NoCompression: return "raw";
        case Compression::ETC2: return "etc2";
        case Compression::BC5: return "bc5";
        case Compression::BC7: return "bc7";
        default: return "unknown";
        }
    }

    std::filesystem::path BuildCompressedPathFromRoots(const std::filesystem::path& inputRoot,
        const std::filesystem::path& outputRoot, const std::filesystem::path& sourceFile, const Compression compression)
    {
        // Fall back to filename only if we cannot find a relative path.
        std::filesystem::path relative = sourceFile.filename();
        std::error_code errorCode;
        const std::filesystem::path weakInputRoot = std::filesystem::weakly_canonical(inputRoot, errorCode);
        const std::filesystem::path weakSource = std::filesystem::weakly_canonical(sourceFile, errorCode);
        if (!errorCode && !weakInputRoot.empty() && !weakSource.empty())
        {
            std::filesystem::path candidate = std::filesystem::relative(weakSource, weakInputRoot, errorCode);
            if (!errorCode)
            {
                relative = candidate;
            }
        }

        // Output path layout and appends a compression tag.
        std::filesystem::path outputPath = outputRoot / relative;
        outputPath.replace_extension(std::string(".") + GetCompressionSuffix(compression) + ".ktx2");
        return outputPath;
    }

    std::filesystem::path BuildCompressedPathFromSource(const std::filesystem::path& sourceFile,
        const Compression compression)
    {
        std::filesystem::path dataRoot;
        for (std::filesystem::path current = sourceFile.parent_path(); !current.empty(); current = current.parent_path())
        {
            if (current.filename() == "data")
            {
                dataRoot = current;
                break;
            }

            if (current == current.root_path())
            {
                break;
            }
        }

        if (!dataRoot.empty())
        {
            return BuildCompressedPathFromRoots(dataRoot, dataRoot / ".compressed", sourceFile, compression);
        }

        // Fallback for external assets: create a .compressed folder near the source.
        const std::filesystem::path fallbackOutputRoot = sourceFile.parent_path() / ".compressed";
        return BuildCompressedPathFromRoots(sourceFile.parent_path(), fallbackOutputRoot, sourceFile, compression);
    }

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
        ktxTexture2* texture = nullptr;
        const std::filesystem::path compressedPath = BuildCompressedPathFromSource(textureLoadingDescription.filePath, textureLoadingDescription.compression);

        if (!std::filesystem::exists(compressedPath))
        {
#if defined(EOS_BUILD_TEXTURE_TOOLS)
            CHECK(EOS::TexturePacking::CompressTextureToCache(
                    textureLoadingDescription.filePath,
                    compressedPath,
                    textureLoadingDescription.compression),
                "Failed to build compressed texture cache '{}' from source '{}'",
                compressedPath.string(),
                textureLoadingDescription.filePath.string());
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
        EOS::Holder<EOS::TextureHandle> loadedTexture = textureLoadingDescription.context->CreateTexture(
        {
            .Type                   = EOS::ImageType::Image_2D,
            .TextureFormat          = textureFormat,
            .TextureDimensions      = {texture->baseWidth, texture->baseHeight},
            .NumberOfMipLevels      = texture->numLevels,
            .Usage                  = EOS::Sampled,
            .Data                   = packedData.data(),
            .DataNumberOfMipLevels  = texture->numLevels,
            .DebugName              = compressedPath.string().c_str(),
        });

        ktxTexture_Destroy(ktxTexture(texture));

        return std::move(loadedTexture);
    }
}
