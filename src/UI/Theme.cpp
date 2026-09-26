#include "UI/Theme.h"
#include "imgui.h"

namespace EOS::UI::Internal
{

    Theme CurrentTheme = Theme::Modern;

    constexpr ImVec4 Canvas          {0.043f, 0.043f, 0.059f, 1.000f};   // window background
    constexpr ImVec4 Surface         {0.078f, 0.078f, 0.106f, 1.000f};   // inputs, buttons, tabs
    constexpr ImVec4 SurfaceHovered  {0.114f, 0.114f, 0.153f, 1.000f};
    constexpr ImVec4 SurfaceActive   {0.149f, 0.149f, 0.200f, 1.000f};
    constexpr ImVec4 SurfaceRaised   {0.098f, 0.098f, 0.129f, 1.000f};   // title bars, menu bar
    constexpr ImVec4 Hairline        {0.137f, 0.137f, 0.180f, 1.000f};   // the one border weight used

    constexpr ImVec4 Accent          {0.486f, 0.424f, 0.969f, 1.000f};
    constexpr ImVec4 AccentHovered   {0.561f, 0.510f, 0.976f, 1.000f};
    constexpr ImVec4 AccentActive    {0.396f, 0.325f, 0.937f, 1.000f};
    constexpr ImVec4 AccentSelection {0.486f, 0.424f, 0.969f, 0.320f};   // text selection, drop target
    constexpr ImVec4 AccentFaint     {0.486f, 0.424f, 0.969f, 0.180f};   // resize grip at rest

    constexpr ImVec4 TextPrimary     {0.929f, 0.929f, 0.949f, 1.000f};
    constexpr ImVec4 TextMuted       {0.541f, 0.541f, 0.604f, 1.000f};

    constexpr ImVec4 Transparent     {0.000f, 0.000f, 0.000f, 0.000f};

    void ApplyModernStyle(ImGuiStyle& style)
        {
            style.WindowPadding     = ImVec2(16.0f, 14.0f);
            style.FramePadding      = ImVec2(12.0f, 7.0f);
            style.CellPadding       = ImVec2(10.0f, 6.0f);
            style.ItemSpacing       = ImVec2(12.0f, 9.0f);
            style.ItemInnerSpacing  = ImVec2(8.0f, 7.0f);
            style.IndentSpacing     = 24.0f;
            style.ScrollbarSize     = 10.0f;
            style.GrabMinSize       = 14.0f;

            style.WindowBorderSize  = 1.0f;
            style.ChildBorderSize   = 0.0f;
            style.PopupBorderSize   = 1.0f;
            style.FrameBorderSize   = 0.0f;
            style.TabBarBorderSize  = 1.0f;

            style.WindowRounding    = 12.0f;
            style.ChildRounding     = 10.0f;
            style.FrameRounding     = 8.0f;
            style.PopupRounding     = 12.0f;
            style.ScrollbarRounding = 10.0f;
            style.GrabRounding      = 8.0f;
            style.TabRounding       = 8.0f;

            style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_None;   // no collapse arrow crowding the title
            style.SeparatorTextBorderSize  = 1.0f;
            style.SeparatorTextPadding     = ImVec2(18.0f, 6.0f);
            style.SeparatorTextAlign       = ImVec2(0.0f, 0.5f);

            style.DisabledAlpha     = 0.40f;

            style.AntiAliasedLines       = true;
            style.AntiAliasedLinesUseTex = true;
            style.AntiAliasedFill        = true;

            ImVec4* colours = style.Colors;

            colours[ImGuiCol_Text]                  = TextPrimary;
            colours[ImGuiCol_TextDisabled]          = TextMuted;

            colours[ImGuiCol_WindowBg]              = Canvas;
            colours[ImGuiCol_ChildBg]               = Transparent;
            // Popups sit above everything, so they get the brightest surface and full opacity.
            colours[ImGuiCol_PopupBg]               = ImVec4(0.086f, 0.086f, 0.118f, 1.000f);
            colours[ImGuiCol_Border]                = Hairline;
            colours[ImGuiCol_BorderShadow]          = Transparent;

            colours[ImGuiCol_FrameBg]               = Surface;
            colours[ImGuiCol_FrameBgHovered]        = SurfaceHovered;
            colours[ImGuiCol_FrameBgActive]         = SurfaceActive;

            // Only a slight lift when the window takes focus, so focusing does not flash the header.
            colours[ImGuiCol_TitleBg]               = SurfaceRaised;
            colours[ImGuiCol_TitleBgActive]         = SurfaceHovered;
            colours[ImGuiCol_TitleBgCollapsed]      = SurfaceRaised;
            colours[ImGuiCol_MenuBarBg]             = SurfaceRaised;

            // No trough: the bar only appears where the grab is.
            colours[ImGuiCol_ScrollbarBg]           = Transparent;
            colours[ImGuiCol_ScrollbarGrab]         = ImVec4(0.180f, 0.180f, 0.235f, 1.000f);
            colours[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.239f, 0.239f, 0.306f, 1.000f);
            colours[ImGuiCol_ScrollbarGrabActive]   = Accent;

            // Accent marks the value, never the whole control.
            colours[ImGuiCol_CheckMark]             = Accent;
            colours[ImGuiCol_SliderGrab]            = Accent;
            colours[ImGuiCol_SliderGrabActive]      = AccentActive;

            colours[ImGuiCol_Button]                = Surface;
            colours[ImGuiCol_ButtonHovered]         = SurfaceHovered;
            colours[ImGuiCol_ButtonActive]          = SurfaceActive;

            colours[ImGuiCol_Header]                = Surface;
            colours[ImGuiCol_HeaderHovered]         = SurfaceHovered;
            colours[ImGuiCol_HeaderActive]          = SurfaceActive;

            colours[ImGuiCol_Separator]             = Hairline;
            colours[ImGuiCol_SeparatorHovered]      = AccentHovered;
            colours[ImGuiCol_SeparatorActive]       = Accent;

            colours[ImGuiCol_ResizeGrip]            = AccentFaint;
            colours[ImGuiCol_ResizeGripHovered]     = AccentHovered;
            colours[ImGuiCol_ResizeGripActive]      = AccentActive;

            // The selected tab is marked by the accent overline rather than by a louder fill.
            colours[ImGuiCol_Tab]                   = Transparent;
            colours[ImGuiCol_TabHovered]            = SurfaceHovered;
            colours[ImGuiCol_TabSelected]           = Surface;
            colours[ImGuiCol_TabSelectedOverline]   = Accent;
            colours[ImGuiCol_TabDimmed]             = Transparent;
            colours[ImGuiCol_TabDimmedSelected]     = SurfaceRaised;
            colours[ImGuiCol_TabDimmedSelectedOverline] = AccentSelection;

            colours[ImGuiCol_PlotLines]             = Accent;
            colours[ImGuiCol_PlotLinesHovered]      = AccentHovered;
            colours[ImGuiCol_PlotHistogram]         = Accent;
            colours[ImGuiCol_PlotHistogramHovered]  = AccentHovered;

            colours[ImGuiCol_TableHeaderBg]         = SurfaceRaised;
            colours[ImGuiCol_TableBorderStrong]     = Hairline;
            colours[ImGuiCol_TableBorderLight]      = ImVec4(0.106f, 0.106f, 0.141f, 1.000f);
            colours[ImGuiCol_TableRowBg]            = Transparent;
            colours[ImGuiCol_TableRowBgAlt]         = ImVec4(1.000f, 1.000f, 1.000f, 0.022f);

            colours[ImGuiCol_TextSelectedBg]        = AccentSelection;
            colours[ImGuiCol_DragDropTarget]        = AccentHovered;
            colours[ImGuiCol_NavCursor]             = Accent;
            colours[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 1.000f, 1.000f, 0.700f);
            // The dim behind a modal is nearly opaque, so the focused panel reads as the only live surface.
            colours[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.016f, 0.016f, 0.024f, 0.700f);
            colours[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.016f, 0.016f, 0.024f, 0.700f);
        }

    void ApplyTheme(const Theme theme)
    {
        CurrentTheme = theme;

        ImGuiStyle& style = ImGui::GetStyle();

        switch (theme)
        {
            case Theme::Modern:
                ApplyModernStyle(style);
                return;

            case Theme::ImGuiDark:
                style = ImGuiStyle();
                ImGui::StyleColorsDark(&style);
                return;

            case Theme::ImGuiLight:
                style = ImGuiStyle();
                ImGui::StyleColorsLight(&style);
                return;
        }

        ApplyModernStyle(style);
    }

    void ReapplyCurrentTheme()
    {
        ApplyTheme(CurrentTheme);
    }
}