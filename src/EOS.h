#pragma once

#include <memory>
#include <string>

#include "defines.h"
#include "window.h"

namespace EOS
{
    //Forward declare
    class IContext;

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

    class IContext
    {
    protected:
        IContext() = default;

    public:
        DELETE_COPY_MOVE(IContext);
        virtual ~IContext() = default;
    };

    struct ContextCreationDescription final
    {
        ContextConfiguration    config;
        void*                   window{};
        void*                   display{};
        HardwareDeviceType      preferredHardwareType{HardwareDeviceType::Discrete};
    };

    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription);
}