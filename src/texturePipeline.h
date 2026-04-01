#pragma once

#include <filesystem>
#include "enums.h"
#include "EOS.h"

namespace EOS::TexturePipeline
{
    // Return the suffix for a given compression mode, used in the output filename.
    [[nodiscard]] const char* GetCompressionSuffix(const Compression compression);


    [[nodiscard]] std::filesystem::path BuildCompressedPathFromRoots(
        const std::filesystem::path& inputRoot,
        const std::filesystem::path& outputRoot,
        const std::filesystem::path& sourceFile,
        const Compression compression);

    [[nodiscard]] std::filesystem::path BuildCompressedPathFromSource(const std::filesystem::path& sourceFile, const Compression compression);

    [[nodiscard]] static EOS::Format KtxVkFormatToEOSFormat(const uint32_t vkFormat);

    // Load a texture from source or cache, optionally compressing on demand if tools are built
    [[nodiscard]] Holder<EOS::TextureHandle> LoadTexture(const EOS::TextureLoadingDescription& textureLoadingDescription);
}
