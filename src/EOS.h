#pragma once

#include <memory>
#include <string>

#include "defines.h"
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

#pragma region ENUMS
    enum class HardwareDeviceType : uint8_t
    {
        Integrated  = 1,
        Discrete    = 2,
        Virtual     = 3,
        Software    = 4
    };

    enum class ColorSpace : uint8_t
    {
        SRGB_Linear,
        SRGB_NonLinear
    };

    enum class ImageType : uint8_t
    {
        Image_1D        = 0,
        Image_2D        = 1,
        Image_3D        = 2,
        CubeMap         = 3,
        Image_1D_Array  = 4,
        Image_2D_Array  = 5,
        CubeMap_Array   = 6,
        SwapChain       = 7,
    };

#pragma endregion

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

#pragma region INTERFACES

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

    protected:
        IContext() = default;
    };

#pragma endregion



    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription);
}