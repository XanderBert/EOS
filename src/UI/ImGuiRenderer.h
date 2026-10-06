#pragma once
#include "defines.h"
#include "EOS.h"
#include ".generated/eos/imgui.h"    // ImGuiTextureView, ImGuiPushConstants

namespace EOS
{
    [[nodiscard]] uint64_t MakeImGuiTextureID(TextureHandle texture, uint32_t layer = 0, ImGuiTextureView view = ImGuiTextureView::Texture2D);

    class ImGuiRenderer final
    {
    public:
        ImGuiRenderer(IContext* context ,const Window& window, const char* defaultFont = "", float fontSize = 8);
        ~ImGuiRenderer();
        DELETE_COPY_MOVE(ImGuiRenderer)

        void SetFont(const char* defaultFont, float fontSize);
        void SetScale(float scale);
        void BeginFrame(ICommandBuffer& cmd);
        void EndFrame(ICommandBuffer& cmd);

    private:
        void CreateNewPipeline(const Framebuffer& framebuffer);
        void SetScaleInternal();

    private:
        IContext* Context;
        SamplerHolder Sampler;
        ShaderProgramHolder Shader;
        TextureHolder FontTexture;
        RenderPipelineHolder RenderPipeline;
        uint32_t FrameIndex = 0;

        float Scale = 1.0f;
        float PendingScale = 1.0f;
        const char* CurrentFont;
        float BaseFontSize;

        struct DrawableData final
        {
            BufferHolder VertexBuffer;
            BufferHolder IndexBuffer;
            uint32_t NumAllocatedIndices = 0;
            uint32_t NumAllocatedVertices = 0;
        };
        DrawableData Drawables[3] = {};
    };
}
