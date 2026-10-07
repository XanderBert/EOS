#include "UI.h"
#include <cstdarg>

#if defined(EOS_USE_IMGUI)
#include "imgui.h"

#include "UI/ImGuiRenderer.h"
#include "UI/Theme.h"
#endif

namespace EOS::UI
{
    void SetTheme([[maybe_unused]] const Theme theme)
    {
#if defined(EOS_USE_IMGUI)
        Internal::ApplyTheme(theme);
#endif
    }

    uint64_t MakeTextureID([[maybe_unused]] const TextureHandle texture, [[maybe_unused]] const uint32_t layer, [[maybe_unused]] const TextureView view)
    {
#if defined(EOS_USE_IMGUI)

        ImGuiTextureView imguiView{ImGuiTextureView::Texture2D};
        if (view == TextureView::Texture2DArray)  imguiView = ImGuiTextureView::Texture2DArray;

        return MakeImGuiTextureID(texture, layer, imguiView);
#else
        return 0;
#endif
    }

    struct Renderer::Implementation final
    {
#if defined(EOS_USE_IMGUI)
        Implementation(IContext* context, const Window& window, const char* defaultFont, const float fontSize)
        : ImGui(context, window, defaultFont, fontSize) {}

        ImGuiRenderer ImGui;
#endif
    };

    Renderer::Renderer([[maybe_unused]] IContext* context, [[maybe_unused]] const Window& window, [[maybe_unused]] const char* defaultFont, [[maybe_unused]] const float fontSize)
#if defined(EOS_USE_IMGUI)
    : Impl(std::make_unique<Implementation>(context, window, defaultFont, fontSize))
#endif
    {
    }

    // Out of line so that Implementation only has to be complete here, and not everywhere UI.h is included.
    Renderer::~Renderer() = default;

    void Renderer::SetFont([[maybe_unused]] const char* defaultFont, [[maybe_unused]] const float fontSize) const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.SetFont(defaultFont, fontSize);
#endif
    }

    void Renderer::SetScale([[maybe_unused]] const float scale) const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.SetScale(scale);
#endif
    }

    void Renderer::NewFrame() const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.NewFrame();
#endif
    }

    void Renderer::Render([[maybe_unused]] ICommandBuffer& commandBuffer) const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.Render(commandBuffer);
#endif
    }

    void Renderer::BeginFrame([[maybe_unused]] ICommandBuffer& commandBuffer) const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.BeginFrame(commandBuffer);
#endif
    }

    void Renderer::EndFrame([[maybe_unused]] ICommandBuffer& commandBuffer) const
    {
#if defined(EOS_USE_IMGUI)
        Impl->ImGui.EndFrame(commandBuffer);
#endif
    }

    bool Begin([[maybe_unused]] const char* name)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::Begin(name);
#else
        return false;
#endif
    }

    void End()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::End();
#endif
    }

    void SetNextWindowSize([[maybe_unused]] const float width, [[maybe_unused]] const float height, [[maybe_unused]] const bool onlyOnFirstUse)
    {
#if defined(EOS_USE_IMGUI)
        ImGui::SetNextWindowSize(ImVec2(width, height), onlyOnFirstUse ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
#endif
    }

    void Separator()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::Separator();
#endif
    }

    void Text([[maybe_unused]] const char* format, ...)
    {
#if defined(EOS_USE_IMGUI)
        va_list arguments;
        va_start(arguments, format);
        ImGui::TextV(format, arguments);
        va_end(arguments);
#endif
    }

    bool Checkbox([[maybe_unused]] const char* label, [[maybe_unused]] bool* value)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::Checkbox(label, value);
#else
        return false;
#endif
    }

    bool SliderFloat([[maybe_unused]] const char* label, [[maybe_unused]] float* value, [[maybe_unused]] const float minimum, [[maybe_unused]] const float maximum)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::SliderFloat(label, value, minimum, maximum);
#else
        return false;
#endif
    }

    bool SliderFloat3([[maybe_unused]] const char* label, [[maybe_unused]] float* values, [[maybe_unused]] const float minimum, [[maybe_unused]] const float maximum)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::SliderFloat3(label, values, minimum, maximum);
#else
        return false;
#endif
    }

    bool SliderInt([[maybe_unused]] const char* label, [[maybe_unused]] int* value, [[maybe_unused]] const int minimum, [[maybe_unused]] const int maximum)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::SliderInt(label, value, minimum, maximum);
#else
        return false;
#endif
    }

    bool DragFloat2([[maybe_unused]] const char* label, [[maybe_unused]] float* values)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::DragFloat2(label, values);
#else
        return false;
#endif
    }

    bool DragFloat3([[maybe_unused]] const char* label, [[maybe_unused]] float* values)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::DragFloat3(label, values);
#else
        return false;
#endif
    }

    bool ColorEdit3([[maybe_unused]] const char* label, [[maybe_unused]] float* colour)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::ColorEdit3(label, colour);
#else
        return false;
#endif
    }

    bool Combo([[maybe_unused]] const char* label, [[maybe_unused]] int* currentItem, [[maybe_unused]] const char* const* items, [[maybe_unused]] const int itemCount)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::Combo(label, currentItem, items, itemCount);
#else
        return false;
#endif
    }

    void Image([[maybe_unused]] const uint64_t textureID, [[maybe_unused]] const float width, [[maybe_unused]] const float height)
    {
#if defined(EOS_USE_IMGUI)
        ImGui::Image(textureID, ImVec2(width, height));
#endif
    }

    bool WantCaptureMouse()
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::GetIO().WantCaptureMouse;
#else
        return false;
#endif
    }

    bool WantCaptureKeyboard()
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::GetIO().WantCaptureKeyboard;
#else
        return false;
#endif
    }

    void SetMouseInputEnabled([[maybe_unused]] const bool enabled)
    {
#if defined(EOS_USE_IMGUI)
        ImGuiIO& io = ImGui::GetIO();
        if (enabled)
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
        else
        {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
#endif
    }

    void ClearFocus()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::SetWindowFocus(nullptr);
#endif
    }
}
