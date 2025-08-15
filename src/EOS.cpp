#include "EOS.h"

#include "logger.h"
#include "utils.h"
#include "vulkan/vulkanClasses.h"
#include "shaders/shaderUtils.h"

namespace EOS
{
    uint32_t VertexInputData::GetNumAttributes() const
    {
        uint32_t n{};
        while (n < MAX_ATTRIBUTES && Attributes[n].Format != VertexFormat::Invalid)
        {
            ++n;
        }

        return n;
    }

    uint32_t VertexInputData::GetNumInputBindings() const
    {
        uint32_t n{};
        while (n < MAX_BUFFERS && InputBindings[n].Stride)
        {
            ++n;
        }
        return n;
    }

    uint32_t VertexInputData::GetVertexSize() const
    {
        uint32_t vertexSize = 0;
        for (uint32_t i{}; i < MAX_ATTRIBUTES && Attributes[i].Format != VertexFormat::Invalid; ++i)
        {
            CHECK(Attributes[i].Offset == vertexSize, "Unsupported vertex attributes format");
            vertexSize += EOS::GetVertexFormatSize(Attributes[i].Format);
        }

        return vertexSize;
    }

    uint32_t SpecializationConstantDescription::GetNumberOfSpecializationConstants() const
    {
        uint32_t n{};
        while (n < MaxSecializationConstants && Entries[n].Size)
        {
            ++n;
        }
        return n;
    }

    uint32_t RenderPipelineDescription::GetNumColorAttachments() const
    {
        uint32_t n{};
        while (n < EOS_MAX_COLOR_ATTACHMENTS && ColorAttachments[n].ColorFormat != Format::Invalid)
        {
            n++;
        }
        return n;
    }

    uint32_t RenderPass::GetNumColorAttachments() const
    {
        uint32_t n{};
        while (n < EOS_MAX_COLOR_ATTACHMENTS && Color[n].LoadOpState != LoadOp::Invalid)
        {
            ++n;
        }
        return n;
    }

    uint32_t Framebuffer::GetNumColorAttachments() const
    {
        uint32_t n{};
        while (n < EOS_MAX_COLOR_ATTACHMENTS && Color[n].Texture)
        {
            ++n;
        }
        return n;
    }

    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription)
    {
        //Initialize the logger
        //TODO: This should happen somewhere, as the shader compiler also uses the loger. what if the end user decides to first create the shader compiler?
        Logger::Init("EOS", ".cache/log.txt");

        return std::move( std::make_unique<VulkanContext>(contextCreationDescription) );
    }

    std::unique_ptr<ShaderCompiler> CreateShaderCompiler(const std::filesystem::path& shaderFolder)
    {
        return std::move(std::make_unique<EOS::ShaderCompiler>(shaderFolder));
    }
}
