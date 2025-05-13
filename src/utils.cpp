#include "utils.h"
#include <fstream>
#include <vector> // Required for string stream approach or reading into a vector
#include <ios>    // Required for std::ios::ate
#include <limits> // Required for numeric_limits

namespace EOS
{
    std::string ReadFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);

        // Check if the file opened successfully
        if (!file.is_open())
        {
            EOS::Logger->error("I/O error. Cannot open file '{}'", filePath.c_str());
            return std::string();
        }

        // Read the entire file content into a string.
        std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());

        file.close();

        // Check if a BOM character exists. If so, replace it with a regular space.
        // BOM (UTF-8): EF BB BF
        static constexpr unsigned char BOM[] = { 0xEF, 0xBB, 0xBF };
        if (content.size() >= 3)
        {
            // Compare the first 3 bytes of the string with the BOM
            if (memcmp(content.data(), BOM, 3) == 0)
            {
                // Replace the BOM characters with spaces
                content[0] = ' ';
                content[1] = ' ';
                content[2] = ' ';
            }
        }

        return content;
    }

    void WriteFile(const std::filesystem::path& filePath, const std::string& content)
    {
        std::ofstream out( filePath, std::ios::out | std::ios::binary);
        if (!out.is_open())
        {
            EOS::Logger->error("I/O error. Cannot Open File '{}'", filePath.c_str());
        }

        out.write(reinterpret_cast<const char*>(content.data()), content.size());
        out.close();
    }
}


