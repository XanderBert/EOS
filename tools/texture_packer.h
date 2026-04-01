#pragma once

#include <filesystem>

#include "enums.h"

namespace EOS::TexturePacking
{
    [[nodiscard]] bool CompressTextureToCache(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        Compression compression);
}