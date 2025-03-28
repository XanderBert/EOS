#include "EOS.h"
#include "vulkan/vulkanClasses.h"

namespace EOS
{
    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription)
    {
        return std::move( std::make_unique<VulkanContext>(contextCreationDescription) );
    }
}
