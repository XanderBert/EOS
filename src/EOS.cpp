#include "EOS.h"

#include "logger.h"
#include "vulkan/vulkanClasses.h"

namespace EOS
{
    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription)
    {
        Logger::Init("EOS");
        return std::move( std::make_unique<VulkanContext>(contextCreationDescription) );
    }
}
