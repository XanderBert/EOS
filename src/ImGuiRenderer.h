#pragma once
#include "defines.h"
#include "EOS.h"

namespace EOS
{
    class ImGuiRenderer final
    {
    public:
        ImGuiRenderer(IContext* context, ShaderCompiler* shaderCompiler ,const Window& window, const char* defaultFont = "", float fontSize = 8);
        ~ImGuiRenderer();
        DELETE_COPY_MOVE(ImGuiRenderer)

        void SetFont(const char* defaultFont, float fontSize);
        void SetScale(float scale);
        void BeginFrame(const Framebuffer& framebuffer);
        void EndFrame(ICommandBuffer& cmd);

    private:
        void CreateNewPipeline(const Framebuffer& framebuffer);

    private:
        IContext* Context;
        SamplerHolder Sampler;
        ShaderModuleHolder VertexShader;
        ShaderModuleHolder FragmentShader;
        TextureHolder FontTexture;
        RenderPipelineHolder RenderPipeline;

        float Scale = 1.0f;
        uint32_t FrameIndex = 0;

        struct DrawableData
        {
            BufferHolder VertexBuffer;
            BufferHolder IndexBuffer;
            uint32_t NumAllocatedIndices = 0;
            uint32_t NumAllocatedVertices = 0;
        };
        DrawableData Drawables[3] = {};

    };
}


#if defined(EOS_USE_IMGUI)
#endif
