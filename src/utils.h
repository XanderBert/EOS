#pragma once
#include <string>
#include "EOS.h"

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
}