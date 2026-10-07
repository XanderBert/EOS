#pragma once
#include <cstdint>
#include <memory>

#include "defines.h"
#include "EOS.h"

namespace EOS
{
    struct Window;
    class GraphFile;

    namespace UI
    {
        // Visual styles the UI can be switched to. Modern is EOS's own dark theme.
        enum class Theme
        {
            Modern = 0,
            ImGuiDark,
            ImGuiLight,
        };

        void SetTheme(Theme theme);

        enum class TextureView : uint32_t
        {
            Texture2D = 0,
            Texture2DArray = 1,
        };

        // Encodes a texture, and optionally an array layer, into the opaque id that Image() takes.
        // Returns 0 when the UI is disabled, since nothing will draw it.
        [[nodiscard]] uint64_t MakeTextureID(TextureHandle texture, uint32_t layer = 0, TextureView view = TextureView::Texture2D);

        class Renderer final
        {
        public:
            Renderer(IContext* context, const Window& window, const char* defaultFont = "", float fontSize = 8);
            ~Renderer();
            DELETE_COPY_MOVE(Renderer)

            void SetFont(const char* defaultFont, float fontSize) const;
            void SetScale(float scale) const;

            // Starts the UI of a frame; widgets can be declared from here on.
            void NewFrame() const;
            // Draws the frame's UI into the color target being rendered to (the swapchain), e.g. in a render graph pass.
            void Render(ICommandBuffer& commandBuffer) const;

            // NewFrame() and Render(), plus rendering onto the swapchain, for callers without a render graph.
            void BeginFrame(ICommandBuffer& commandBuffer) const;
            void EndFrame(ICommandBuffer& commandBuffer) const;

        private:
            // Holds the ImGui backed implementation, and holds nothing at all when the UI is disabled.
            struct Implementation;
            std::unique_ptr<Implementation> Impl;
        };

        bool Begin(const char* name);
        void End();
        void SetNextWindowSize(float width, float height, bool onlyOnFirstUse = true);

        void Separator();
        void Text(const char* format, ...);

        // Widgets with the same label need different IDs: everything between PushID and PopID is told apart by id.
        void PushID(const char* id);
        void PopID();
        void Indent();
        void Unindent();

        bool Checkbox(const char* label, bool* value);
        bool SliderFloat(const char* label, float* value, float minimum, float maximum);
        bool SliderFloat2(const char* label, float* values, float minimum, float maximum);
        bool SliderFloat3(const char* label, float* values, float minimum, float maximum);
        bool SliderFloat4(const char* label, float* values, float minimum, float maximum);
        bool SliderInt(const char* label, int* value, int minimum, int maximum);
        bool DragFloat(const char* label, float* value);
        bool DragFloat2(const char* label, float* values);
        bool DragFloat3(const char* label, float* values);
        bool DragFloat4(const char* label, float* values);
        bool DragInt(const char* label, int* value);
        bool ColorEdit3(const char* label, float* colour);
        bool Combo(const char* label, int* currentItem, const char* const* items, int itemCount);

        template<size_t ItemCount>
        bool Combo(const char* label, int* currentItem, const char* const (&items)[ItemCount])
        {
            return Combo(label, currentItem, items, static_cast<int>(ItemCount));
        }

        void Image(uint64_t textureID, float width, float height);

        // Every pass of a graph file with an enable checkbox and a widget per property, sliders where the property
        // has a range. Edits last until the file is reloaded.
        void GraphFileProperties(GraphFile& file);

        [[nodiscard]] bool WantCaptureMouse();
        [[nodiscard]] bool WantCaptureKeyboard();

        void SetMouseInputEnabled(bool enabled);

        // Drops focus from every UI window so keyboard input goes back to the application.
        void ClearFocus();
    }
}
