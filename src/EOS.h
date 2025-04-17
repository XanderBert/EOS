#pragma once

#include <memory>
#include <string>

#include "defines.h"
#include "enums.h"
#include "handle.h"
#include "window.h"

namespace EOS
{
    //TODO: Almost everything here will need Doxygen documentation, as the end user will work with these functions
    //I would also like to return my own type of VkResult on functions for the end user.

    //Forward declare
    class IContext;

    //Create our Handle structures
    using ComputePipelineHandle     = Handle<struct ComputePipeline>;
    using RenderPipelineHandle      = Handle<struct RenderPipeline>;
    using RayTracingPipelineHandle  = Handle<struct RayTracingPipeline>;
    using ShaderModuleHandle        = Handle<struct ShaderModule>;
    using SamplerHandle             = Handle<struct Sampler>;
    using BufferHandle              = Handle<struct Buffer>;
    using TextureHandle             = Handle<struct Texture>;
    using QueryPoolHandle           = Handle<struct QueryPool>;
    using AccelStructHandle         = Handle<struct AccelerationStructure>;

    struct HardwareDeviceDescription
    {
        uintptr_t id{};
        HardwareDeviceType type { HardwareDeviceType::Integrated} ;
        std::string name{};
    };

    struct ContextConfiguration final
    {
        bool enableValidationLayers{ true };
        ColorSpace DesiredSwapChainColorSpace { ColorSpace::SRGB_Linear };
    };

    struct ContextCreationDescription final
    {
        ContextConfiguration    config;
        HardwareDeviceType      preferredHardwareType{HardwareDeviceType::Discrete};
        const char*             applicationName{};
        void*                   window{};
        void*                   display{};
    };

    //used for buffer StateTransitioning without changing the index queue
    struct GlobalBarrier
    {
        const BufferHandle      Buffer;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

    struct ImageBarrier
    {
        const TextureHandle     Texture;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

#pragma region INTERFACES
    //TODO: instead of interfaces use concept and a forward declare. And then every API implements 1 class of that name with the concept.
    //CMake should handle that only 1 type of API is being used at the time.
    //This way we can completely get rid of inheritance

    class ICommandBuffer
    {
    public:
        DELETE_COPY_MOVE(ICommandBuffer);
        virtual ~ICommandBuffer() = default;

    protected:
        ICommandBuffer() = default;
    };

    class IContext
    {
    public:
        DELETE_COPY_MOVE(IContext);
        virtual ~IContext() = default;

        /**
        * @brief
        * @return
        */
        virtual ICommandBuffer& AcquireCommandBuffer() = 0;

        /**
        * @brief
        * @return
        */
        virtual SubmitHandle Submit(ICommandBuffer& commandBuffer, TextureHandle present) = 0;

        /**
         * @brief 
         * @return 
         */
        virtual TextureHandle GetSwapChainTexture() = 0;
    
    protected:
        IContext() = default;
    };
#pragma endregion
    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription);
}

#pragma region GLOBAL_FUNCTIONS
/**
* @brief
* @return
*/
void cmdPipelineBarrier(const EOS::ICommandBuffer& commandBuffer, const std::vector<EOS::GlobalBarrier>& globalBarriers, const std::vector<EOS::ImageBarrier>& imageBarriers);
#pragma endregion