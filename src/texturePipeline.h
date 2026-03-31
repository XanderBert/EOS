#pragma once

#include <filesystem>
#include <string>

#include "enums.h"

namespace EOS::TexturePipeline
{
    // Return the suffix for a given compression mode, used in the output filename.
    [[nodiscard]] inline const char* GetCompressionSuffix(const Compression compression)
    {
        switch (compression)
        {
            case Compression::NoCompression: return "raw";
            case Compression::ETC2: return "etc2";
            case Compression::BC5: return "bc5";
            case Compression::BC7: return "bc7";
            default: return "unknown";
        }
    }

    [[nodiscard]] inline std::filesystem::path BuildCompressedPathFromRoots(
        const std::filesystem::path& inputRoot,
        const std::filesystem::path& outputRoot,
        const std::filesystem::path& sourceFile,
        const Compression compression)
    {
        // Fall back to filename only if we cannot find a relative path.
        std::filesystem::path relative = sourceFile.filename();
        std::error_code errorCode;
        const std::filesystem::path weakInputRoot = std::filesystem::weakly_canonical(inputRoot, errorCode);
        const std::filesystem::path weakSource = std::filesystem::weakly_canonical(sourceFile, errorCode);
        if (!errorCode && !weakInputRoot.empty() && !weakSource.empty())
        {
            std::filesystem::path candidate = std::filesystem::relative(weakSource, weakInputRoot, errorCode);
            if (!errorCode)
            {
                relative = candidate;
            }
        }

        // Output path layout and appends a compression tag.
        std::filesystem::path outputPath = outputRoot / relative;
        outputPath.replace_extension(std::string(".") + GetCompressionSuffix(compression) + ".ktx2");
        return outputPath;
    }

    [[nodiscard]] inline std::filesystem::path BuildCompressedPathFromSource(const std::filesystem::path& sourceFile, const Compression compression)
    {
        std::filesystem::path dataRoot;
        for (std::filesystem::path current = sourceFile.parent_path(); !current.empty(); current = current.parent_path())
        {
            if (current.filename() == "data")
            {
                dataRoot = current;
                break;
            }

            if (current == current.root_path())
            {
                break;
            }
        }

        if (!dataRoot.empty())
        {
            return BuildCompressedPathFromRoots(dataRoot, dataRoot / ".compressed", sourceFile, compression);
        }

        // Fallback for external assets: create a .compressed folder near the source.
        const std::filesystem::path fallbackOutputRoot = sourceFile.parent_path() / ".compressed";
        return BuildCompressedPathFromRoots(sourceFile.parent_path(), fallbackOutputRoot, sourceFile, compression);
    }
}
