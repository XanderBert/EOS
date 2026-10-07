#pragma once

#include <array>
#include <string_view>

#include "enums.h"

// Texture formats by name, for formats written as text: render graph files and the [Format] attribute of Slang passes.
// "swapchain" stands for the swapchain's format, whatever it is (Format::Invalid).
namespace EOS
{
    struct FormatName final
    {
        std::string_view Name;
        Format Value;
    };

    inline constexpr std::array kFormatNames
    {
        FormatName{"swapchain", Format::Invalid},
        FormatName{"R_UN8", Format::R_UN8}, FormatName{"R_UI16", Format::R_UI16}, FormatName{"R_UI32", Format::R_UI32},
        FormatName{"R_UN16", Format::R_UN16}, FormatName{"R_F16", Format::R_F16}, FormatName{"R_F32", Format::R_F32},
        FormatName{"RG_UN8", Format::RG_UN8}, FormatName{"RG_UI16", Format::RG_UI16}, FormatName{"RG_UI32", Format::RG_UI32},
        FormatName{"RG_UN16", Format::RG_UN16}, FormatName{"RG_F16", Format::RG_F16}, FormatName{"RG_F32", Format::RG_F32},
        FormatName{"RGBA_UN8", Format::RGBA_UN8}, FormatName{"RGBA_UI32", Format::RGBA_UI32}, FormatName{"RGBA_F16", Format::RGBA_F16},
        FormatName{"RGBA_F32", Format::RGBA_F32}, FormatName{"RGBA_SRGB8", Format::RGBA_SRGB8}, FormatName{"BGRA_UN8", Format::BGRA_UN8},
        FormatName{"BGRA_SRGB8", Format::BGRA_SRGB8},
        FormatName{"Z_UN16", Format::Z_UN16}, FormatName{"Z_UN24", Format::Z_UN24}, FormatName{"Z_F32", Format::Z_F32},
        FormatName{"Z_UN24_S_UI8", Format::Z_UN24_S_UI8}, FormatName{"Z_F32_S_UI8", Format::Z_F32_S_UI8},
    };

    // Null when there is no format with that name.
    [[nodiscard]] constexpr const FormatName* FindFormat(std::string_view name)
    {
        for (const FormatName& format : kFormatNames)
        {
            if (format.Name == name) return &format;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool IsDepthFormat(Format format)
    {
        return format == Format::Z_UN16 || format == Format::Z_UN24 || format == Format::Z_F32 || format == Format::Z_UN24_S_UI8 || format == Format::Z_F32_S_UI8;
    }
}
