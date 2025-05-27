#pragma once
#include <cassert>
#include <filesystem>
#include <string>

#include "enums.h"


namespace EOS
{
    /**
    * @brief
    * @return
    */
    std::string ReadFile(const std::filesystem::path& filePath);

    /**
    * @brief
    * @return
    */
    void WriteFile(const std::filesystem::path& filePath, const std::string& content);


    uint32_t GetVertexFormatSize(const EOS::VertexFormat format);
}
