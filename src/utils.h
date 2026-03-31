#pragma once
#include <filesystem>
#include <string>

#include "enums.h"
#include "EOS.h"


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

    [[nodiscard]] uint64_t GetAddressAligned(uint64_t address, uint64_t alignment);
    
    [[nodiscard]] EOS::Holder<EOS::TextureHandle> LoadTexture(const TextureLoadingDescription& textureLoadingDescription);

    [[nodiscard]] constexpr uint32_t CalculateNumberOfMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels) levels++;

        return levels;
    }

    [[nodiscard]] std::filesystem::file_time_type GetLastWriteTime(const std::string& fileName);
}
