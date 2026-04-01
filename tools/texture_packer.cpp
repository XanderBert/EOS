#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "enums.h"
#include "texturePipeline.h"

#include "ktx.h"
#include <volk.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize2.h>

namespace
{
    struct CompressionInfo final
    {
        ktx_transcode_fmt_e transcodeFormat = KTX_TTF_NOSELECTION;
        bool useUASTC = false;
        bool normalMap = false;
    };

    struct Job final
    {
        std::filesystem::path sourcePath;
        std::filesystem::path outputPath;
        EOS::Compression compression = EOS::Compression::NoCompression;
    };

    [[nodiscard]] uint32_t CalculateNumberOfMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while ((width | height) >> levels)
        {
            ++levels;
        }

        return levels;
    }

    [[nodiscard]] CompressionInfo ResolveCompressionInfo(const EOS::Compression compression)
    {
        switch (compression)
        {
            case EOS::Compression::ETC2: return { KTX_TTF_ETC2_RGBA, false, false };
            case EOS::Compression::BC5: return { KTX_TTF_BC5_RG, true, true };
            case EOS::Compression::BC7: return { KTX_TTF_BC7_RGBA, false, false };
            case EOS::Compression::NoCompression:
            default: return { KTX_TTF_NOSELECTION, false, false };
        }
    }

    [[nodiscard]] bool HasSupportedImageExtension(const std::filesystem::path& path)
    {
        //Get the extension
        std::string extension = path.extension().string();

        // Make extension lower case
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return extension == ".png"
            || extension == ".jpg"
            || extension == ".jpeg"
            || extension == ".tga"
            || extension == ".bmp"
            || extension == ".hdr";
    }

    [[nodiscard]] bool NeedsRebuild(const std::filesystem::path& sourcePath, const std::filesystem::path& outputPath)
    {
        std::error_code errorCode;

        //Check if the file has been compressed already
        if (!std::filesystem::exists(outputPath, errorCode))  return true;
        if (errorCode) return true;

        const auto sourceTime = std::filesystem::last_write_time(sourcePath, errorCode);
        if (errorCode) return true;

        const auto outputTime = std::filesystem::last_write_time(outputPath, errorCode);
        if (errorCode) return true;

        // If the source file is newer then the compressed file, we need to recompress it
        return outputTime < sourceTime;
    }

    [[nodiscard]] ktxTexture2* CompressTexture(const uint8_t* pixels, const int width, const int height, const EOS::Compression compression)
    {
        const CompressionInfo info = ResolveCompressionInfo(compression);
        const uint32_t mipLevels = CalculateNumberOfMipLevels(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

        const ktxTextureCreateInfo createInfo
        {
            .glInternalformat = 0x8058,
            .vkFormat         = static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_SRGB),
            .baseWidth        = static_cast<uint32_t>(width),
            .baseHeight       = static_cast<uint32_t>(height),
            .baseDepth        = 1u,
            .numDimensions    = 2u,
            .numLevels        = mipLevels,
            .numLayers        = 1u,
            .numFaces         = 1u,
            .generateMipmaps  = KTX_FALSE,
        };

        ktxTexture2* texture = nullptr;
        if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        {
            return nullptr;
        }

        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            size_t offset = 0;
            ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset);
            uint8_t* destination = ktxTexture_GetData(ktxTexture(texture)) + offset;

            const int mipWidth = std::max(1, width >> static_cast<int>(level));
            const int mipHeight = std::max(1, height >> static_cast<int>(level));

            if (level == 0)
            {
                const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
                std::memcpy(destination, pixels, bytes);
            }
            else
            {
                stbir_resize_uint8_linear(
                    pixels, width, height, 0,
                    destination, mipWidth, mipHeight, 0,
                    STBIR_RGBA);
            }
        }

        if (info.transcodeFormat != KTX_TTF_NOSELECTION)
        {
            ktxBasisParams basisParams = {};
            basisParams.structSize = sizeof(basisParams);
            basisParams.noSSE = KTX_FALSE;
            basisParams.threadCount = 1;
            basisParams.uastc = info.useUASTC ? KTX_TRUE : KTX_FALSE;
            basisParams.uastcFlags = info.useUASTC ? KTX_PACK_UASTC_LEVEL_DEFAULT : 0;
            basisParams.normalMap = info.normalMap ? KTX_TRUE : KTX_FALSE;

            if (ktxTexture2_CompressBasisEx(texture, &basisParams) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(texture));
                return nullptr;
            }

            if (ktxTexture2_TranscodeBasis(texture, info.transcodeFormat, 0) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(texture));
                return nullptr;
            }
        }

        return texture;
    }

    int CompressJob(const Job& job)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        constexpr int desiredChannels = 4;
        uint8_t* pixels = stbi_load(job.sourcePath.string().c_str(), &width, &height, &channels, desiredChannels);
        if (pixels == nullptr)
        {
            std::cerr << "[error] failed to load " << job.sourcePath << std::endl;
            return 1;
        }

        ktxTexture2* texture = CompressTexture(pixels, width, height, job.compression);
        stbi_image_free(pixels);

        if (texture == nullptr)
        {
            std::cerr << "[error] failed to compress " << job.sourcePath << std::endl;
            return 1;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(job.outputPath.parent_path(), errorCode);
        if (errorCode)
        {
            std::cerr << "[error] failed to create directories for " << job.outputPath << std::endl;
            ktxTexture_Destroy(ktxTexture(texture));
            return 1;
        }

        if (ktxTexture_WriteToNamedFile(ktxTexture(texture), job.outputPath.string().c_str()) != KTX_SUCCESS)
        {
            std::cerr << "[error] failed to write " << job.outputPath << std::endl;
            ktxTexture_Destroy(ktxTexture(texture));
            return 1;
        }

        ktxTexture_Destroy(ktxTexture(texture));
        return 0;
    }

    // Used to skip files inside the output folder by checking whether `path` has `root` as its path
    [[nodiscard]] bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& root)
    {
        const auto pathIt = path.begin();
        const auto rootIt = root.begin();

        auto p = pathIt;
        auto r = rootIt;
        for (; r != root.end(); ++r, ++p)
        {
            if (p == path.end() || *p != *r)
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::vector<Job> BuildJobs(
        const std::filesystem::path& inputRoot,
        const std::filesystem::path& outputRoot,
        const bool includeETC2)
    {
        std::vector<Job> jobs;
        std::error_code errorCode;

        // Check if the folder exists
        if (!std::filesystem::exists(inputRoot, errorCode))  return jobs;

        // Try finding the input and output folder
        const std::filesystem::path weakInputRoot = std::filesystem::weakly_canonical(inputRoot, errorCode);
        const std::filesystem::path weakOutputRoot = std::filesystem::weakly_canonical(outputRoot, errorCode);

        // Setup the Compressions we want to do
        // TODO: i somehow need to find out which texture needs to be compressed in which format.
        // I am thinking of a naming convention like `texture_bc7.png` or `texture_etc2.png` to indicate which compression format to use. 
        // I don't want to force the end user a specific naming convention so if i go this way it should editable trough cmake.
        const std::vector<EOS::Compression> compressionModes = includeETC2
            ? std::vector<EOS::Compression>{ EOS::Compression::BC7, EOS::Compression::BC5, EOS::Compression::ETC2 }
            : std::vector<EOS::Compression>{ EOS::Compression::BC7, EOS::Compression::BC5 };

        // Go over the "entries" in the input directory
        for (const auto& entry : std::filesystem::recursive_directory_iterator(inputRoot))
        {
            // If it's not a file, skip it
            if (!entry.is_regular_file()) continue;

            // Check if its a image format we support
            if (!HasSupportedImageExtension(entry.path())) continue;

            // Skip files in the output folder to avoid processing already compressed textures. 
            if (!weakOutputRoot.empty() && PathStartsWith(entry.path(), weakOutputRoot)) continue;


            for (const EOS::Compression compression : compressionModes)
            {
                // Build the output path for this file and compression mode
                const std::filesystem::path outputPath = EOS::TexturePipeline::BuildCompressedPathFromRoots(
                    weakInputRoot.empty() ? inputRoot : weakInputRoot,
                    weakOutputRoot.empty() ? outputRoot : weakOutputRoot,
                    entry.path(),
                    compression);

                // Check if we need to rebuild this file by comparing the last write time of the source and output. If the output is newer, we can skip it.
                if (!NeedsRebuild(entry.path(), outputPath)) continue;
        
                // If we need to rebuild, add a job for it.
                jobs.push_back(Job{
                    .sourcePath = entry.path(),
                    .outputPath = outputPath,
                    .compression = compression,
                });
            }
        }

        return jobs;
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path inputRoot = "data";
    std::filesystem::path outputRoot = "data/.compressed";
    uint32_t requestedThreads = 0;
    bool includeETC2 = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--input" && i + 1 < argc)
        {
            inputRoot = argv[++i];
            continue;
        }

        if (arg == "--output" && i + 1 < argc)
        {
            outputRoot = argv[++i];
            continue;
        }

        if (arg == "--threads" && i + 1 < argc)
        {
            requestedThreads = static_cast<uint32_t>(std::stoul(argv[++i]));
            continue;
        }

        if (arg == "--include-etc2")
        {
            includeETC2 = true;
            continue;
        }

        std::cerr << "Usage: EOSTextureCompressor [--input <dir>] [--output <dir>] [--threads <N>] [--include-etc2]\n";
        return 1;
    }

    // Build the list of jobs to do 
    std::vector<Job> jobs = BuildJobs(inputRoot, outputRoot, includeETC2);
    if (jobs.empty())
    {
        std::cout << "[texture-packer] no texture updates required" << std::endl;
        return 0;
    }

    const uint32_t threadCount = requestedThreads == 0 ? std::max(1u, std::thread::hardware_concurrency()) : std::max(1u, requestedThreads);
    std::cout << "[texture-packer] processing " << jobs.size() << " jobs on " << threadCount << " threads" << std::endl;

    std::atomic<size_t> nextJob = 0;
    std::atomic<int> failures = 0;
    std::atomic<size_t> finished = 0;
    std::mutex outputMutex;

    // Create a lambda function that a thread will run to process jobs
    auto worker = [&]()
    {
        while (true)
        {
            // Get the next job index atomically
            const size_t index = nextJob.fetch_add(1);
            
            // If the index is out of range, we are done
            if (index >= jobs.size())   break;

            // Process the job
            const Job& job = jobs[index];
            const int result = CompressJob(job);
            
            // If the job failed, increment the failure count
            if (result != 0) failures.fetch_add(1);
  

            //If the job succeeded, print the output path.
            const size_t done = finished.fetch_add(1) + 1;
            std::lock_guard<std::mutex> lock(outputMutex);
            std::cout << "[texture-packer] [" << done << "/" << jobs.size() << "] " << job.sourcePath << " -> " << job.outputPath << std::endl;
        }
    };

    // Create a number of worker threads to process the jobs
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i) workers.emplace_back(worker);

    // Wait for all threads to finish
    for (std::thread& thread : workers) thread.join();


    // If there were any failures, report it and return an error code
    if (failures.load() > 0)
    {
        std::cerr << "[texture-packer] completed with " << failures.load() << " failures" << std::endl;
        return 2;
    }


    std::cout << "[texture-packer] completed successfully" << std::endl;
    return 0;
}
