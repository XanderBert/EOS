#pragma once
#include <cassert>
#include <filesystem>
#include <string>

#include "enums.h"
#include "EOS.h"
#include "ktx.h"


namespace EOS
{

    /**
     * @brief
     * @param filePath
     * @return
     */
    [[nodiscard]] std::string ReadFile(const std::filesystem::path& filePath);

    /**
     * @brief
     * @param filePath
     * @param content
     */
    void WriteFile(const std::filesystem::path& filePath, const std::string& content);

    /**
     * @brief
     * @param format
     * @return
     */
    [[nodiscard]] uint32_t GetVertexFormatSize(const EOS::VertexFormat format);

    /**
     * @brief
     * @param value
     * @param alignment
     * @return
     */
    [[nodiscard]] uint32_t GetSizeAligned(uint32_t value, uint32_t alignment);
    
    [[nodiscard]] EOS::Holder<EOS::TextureHandle> LoadTexture(const TextureLoadingDescription& textureLoadingDescription);

    [[nodiscard]] ktxTexture1* CompressTexture(uint8_t* pixels, int width, int height, Compression compression = NoCompression);

    [[nodiscard]] inline EOS::Format CompressionToFormat(const EOS::Compression compression)
    {
        switch (compression)
        {
        case BC5: return Format::BC5_RG;
        case BC7: return Format::BC7_RGBA;
        case ETC2: return Format::ETC2_SRGB8;
        }

        return Format::RGBA_SRGB8;
    }

    [[nodiscard]] constexpr uint32_t CalculateNumberOfMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels) levels++;

        return levels;
    }
}
