#pragma once

#include "ktx.h"
#include "enums.h"


struct TextureCompressor final
{
    struct CompressionInfo
    {
        ktx_transcode_fmt_e transcodeFormat;
        bool                useUASTC;
        bool                normalMap;
    };

    [[nodiscard]] static ktxTexture2* CompressTexture(uint8_t* pixels, int width, int height, EOS::Compression compression = EOS::NoCompression);
    [[nodiscard]] static EOS::Format CompressionToFormat(const EOS::Compression compression);
    [[nodiscard]] static CompressionInfo ResolveCompressionInfo(EOS::Compression compression);
};
