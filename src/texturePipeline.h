#pragma once

#include "enums.h"
#include "EOS.h"

namespace EOS::TexturePipeline
{
    [[nodiscard]] static EOS::Format KtxVkFormatToEOSFormat(const uint32_t vkFormat);

    // Load a texture from source or cache, optionally compressing on demand if tools are built
    [[nodiscard]] Holder<EOS::TextureHandle> LoadTexture(const EOS::TextureLoadingDescription& textureLoadingDescription);
}