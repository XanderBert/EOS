#pragma once
#include "defines.h"
#include "EOS.h"

namespace EOS
{
    enum class ImGuiTextureView : uint32_t
    {
        Texture2D = 0,
        Texture2DArray = 1,
    };

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
        ShaderModuleHolder VertexShader;
        ShaderModuleHolder FragmentShader;
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

        struct BindData final
        {
            float LRTB[4];
            uint64_t vertexBufferPtr = 0;
            uint32_t textureId = 0;
            uint32_t samplerId = 0;
            uint32_t textureLayer = 0;
            uint32_t textureView = 0;
        };

    };
}
