#include "EOS.h"

#include "logger.h"
#include "vulkan/vulkanClasses.h"

namespace EOS
{
    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription)
    {
        //Initialize the logger
        //TODO: This should happen somewhere, as the shader compiler also uses the loger. what if the end user decides to first create the shader compiler?
        Logger::Init("EOS", ".cache/log.txt");

        return std::move( std::make_unique<VulkanContext>(contextCreationDescription) );
    }

    std::unique_ptr<ShaderCompiler> CreateShaderCompiler(const std::filesystem::path& shaderFolder)
    {
        return std::move(std::make_unique<ShaderCompiler>(shaderFolder));
    }
}
