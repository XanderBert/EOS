#pragma once
#include <cassert>
#include <filesystem>
#include <string>

#include "enums.h"


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

}
