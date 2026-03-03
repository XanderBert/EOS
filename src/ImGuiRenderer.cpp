#include "ImGuiRenderer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "shaders/shaderUtils.h"

namespace EOS
{
    //TODO: I don't like that i need to pass a shaderCompiler, That should be handled automatically,
    //Also i should be able to call context->LoadShader or sth instead of EOS::LoadShader to be consistent with the api
    ImGuiRenderer::ImGuiRenderer(IContext* context, ShaderCompiler* shaderCompiler , const Window& window, const char* defaultFont, float fontSize)
    :Context(context)
    {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "imgui-EOS";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImGui_ImplGlfw_InitForOther(window.GlfwWindow, window.GlfwWindow ? true : false);
        SetFont(defaultFont, fontSize);


        constexpr SamplerDescription samplerDesc
        {
            .wrapU = SamplerWrap::Clamp,
            .wrapV = SamplerWrap::Clamp,
            .wrapW = SamplerWrap::Clamp,
            .debugName = "Imgui Sampler",
        };
        Sampler = context->CreateSampler(samplerDesc);
        VertexShader    = LoadShader(context, shaderCompiler, "imgui", ShaderStage::Vertex);
        FragmentShader  = LoadShader(context, shaderCompiler, "imgui", ShaderStage::Fragment);
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexRef = ImTextureRef();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiRenderer::SetFont(const char* defaultFont, float fontSize)
    {
        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig cfg = ImFontConfig();
        cfg.FontDataOwnedByAtlas = false;
        cfg.RasterizerMultiply = 1.5f;
        cfg.SizePixels = ceilf(fontSize);
        cfg.PixelSnapH = true;
        cfg.OversampleH = 4;
        cfg.OversampleV = 4;
        ImFont* font = nullptr;

        if (strcmp(defaultFont, "") != 0)
        {
            font = io.Fonts->AddFontFromFileTTF(defaultFont, cfg.SizePixels, &cfg);
        }
        else
        {
            font = io.Fonts->AddFontDefault(&cfg);
        }

        io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        const TextureDescription textureDesc
        {
            .Type = ImageType::Image_2D,
            .TextureFormat = Format::RGBA_UN8,
            .TextureDimensions = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
            .Usage = TextureUsageFlags::Sampled,
            .Data = pixels,
            .DebugName = "Imgui FontTexture"
        };

        FontTexture = Context->CreateTexture(textureDesc);
        io.Fonts->TexID = FontTexture.Index();
        io.FontDefault = font;
    }

    void ImGuiRenderer::SetScale(float scale)
    {
        Scale = scale;
        RenderPipeline.Reset();
    }

    void ImGuiRenderer::BeginFrame(const Framebuffer& framebuffer)
    {
        CHECK(framebuffer.Color[0].Texture, "We need at least 1 color texture in the framebuffer");
        const Dimensions dim = Context->GetDimensions(framebuffer.Color[0].Texture);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(dim.Width / Scale, dim.Height / Scale);
        io.DisplayFramebufferScale = ImVec2(Scale, Scale);
        io.IniFilename = nullptr;

        if (RenderPipeline.Empty()) CreateNewPipeline(framebuffer);

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiRenderer::EndFrame(ICommandBuffer& cmd)
    {
        static_assert(sizeof(ImDrawIdx) == 2);

        ImGui::EndFrame();
        ImGui::Render();

        ImDrawData* dd = ImGui::GetDrawData();

        const float fb_width = dd->DisplaySize.x * dd->FramebufferScale.x;
        const float fb_height = dd->DisplaySize.y * dd->FramebufferScale.y;
        if (fb_width <= 0 || fb_height <= 0 || dd->CmdListsCount == 0)  return;


        cmdPushMarker(cmd, "GUI", 0xff11ff01);
        cmdSetDepthState(cmd, {});
        cmdBindViewport(cmd, {.X = 0.0f, .Y = 0.0f, .Width = fb_width, .Height = fb_height});

        DrawableData& drawableData = Drawables[FrameIndex];
        FrameIndex = (FrameIndex + 1) % ARRAY_COUNT(Drawables);

        //Create index buffer
        if (drawableData.NumAllocatedIndices < dd->TotalIdxCount)
        {
            const BufferDescription indexBufferDesc
            {
                .Usage = BufferUsageFlags::Index,
                .Storage = StorageType::HostVisible,
                .Size = dd->TotalIdxCount * sizeof(ImDrawIdx),
                .DebugName = "Imgui Index Buffer"
            };

            drawableData.IndexBuffer = Context->CreateBuffer(indexBufferDesc);
            drawableData.NumAllocatedIndices = dd->TotalIdxCount;
        }

        //Create Vertex Buffer
        if (drawableData.NumAllocatedVertices < dd->TotalVtxCount)
        {
            const BufferDescription vertexBufferDesc
            {
                .Usage = BufferUsageFlags::StorageFlag,
                .Storage = StorageType::HostVisible,
                .Size = dd->TotalVtxCount * sizeof(ImDrawVert),
                .DebugName = "Imgui Vertex Buffer"
            };

            drawableData.VertexBuffer = Context->CreateBuffer(vertexBufferDesc);
            drawableData.NumAllocatedVertices = dd->TotalVtxCount;
        }

        //Upload vertex/index buffers
        {
            ImDrawVert* vtx = reinterpret_cast<ImDrawVert*>(Context->GetMappedPtr(drawableData.VertexBuffer));
            uint16_t* idx = reinterpret_cast<uint16_t*>(Context->GetMappedPtr(drawableData.IndexBuffer));

            for (const ImDrawList* cmdList : dd->CmdLists)
            {
                memcpy(vtx, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
                memcpy(idx, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
                vtx += cmdList->VtxBuffer.Size;
                idx += cmdList->IdxBuffer.Size;
            }

            Context->FlushMappedMemory(drawableData.VertexBuffer, dd->TotalVtxCount * sizeof(ImDrawVert));
            Context->FlushMappedMemory(drawableData.IndexBuffer, dd->TotalIdxCount * sizeof(ImDrawIdx));
        }

        uint32_t idxOffset = 0;
        uint32_t vtxOffset = 0;

        cmdBindIndexBuffer(cmd, drawableData.IndexBuffer, EOS::IndexFormat::UI16);
        cmdBindRenderPipeline(cmd, RenderPipeline);


        const float L = dd->DisplayPos.x;
        const float R = dd->DisplayPos.x + dd->DisplaySize.x;
        const float T = dd->DisplayPos.y;
        const float B = dd->DisplayPos.y + dd->DisplaySize.y;

        const ImVec2 clip_off = dd->DisplayPos;
        const ImVec2 clip_scale = dd->FramebufferScale;

        for (const ImDrawList* cmdList : dd->CmdLists)
        {
            for (int cmd_i{}; cmd_i < cmdList->CmdBuffer.Size; ++cmd_i)
            {
                const ImDrawCmd ImCmd = cmdList->CmdBuffer[cmd_i];
                //CHECK(ImCmd.UserCallback, "The user callback of the imgui draw command is not valid.");

                ImVec2 clipMin((ImCmd.ClipRect.x - clip_off.x) * clip_scale.x, (ImCmd.ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clipMax((ImCmd.ClipRect.z - clip_off.x) * clip_scale.x, (ImCmd.ClipRect.w - clip_off.y) * clip_scale.y);

                //Check the clip regions -> scissor out
                if (clipMin.x < 0.0f) clipMin.x = 0.0f;
                if (clipMin.y < 0.0f) clipMin.y = 0.0f;
                if (clipMax.x > fb_width ) clipMax.x = fb_width;
                if (clipMax.y > fb_height) clipMax.y = fb_height;
                if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) continue;


                struct ImguiBindData
                {
                    float LRTB[4]; // ortho projection: left, right, top, bottom
                    uint64_t vb = 0;
                    uint32_t textureId = 0;
                    uint32_t samplerId = 0;
                }
                bindData =
                {
                    .LRTB = {L, R, T, B},
                    .vb = Context->GetGPUAddress(drawableData.VertexBuffer),
                    .textureId = static_cast<uint32_t>(ImCmd.GetTexID()),
                    .samplerId = Sampler.Index(),
                };

                cmdPushConstants(cmd, bindData);
                cmdBindScissorRect(cmd, {static_cast<uint32_t>(clipMin.x), static_cast<uint32_t>(clipMin.y), static_cast<uint32_t>(clipMax.x - clipMin.x), static_cast<uint32_t>(clipMax.y - clipMin.y)});
                cmdDrawIndexed(cmd, ImCmd.ElemCount, 1u, idxOffset + ImCmd.IdxOffset, static_cast<int32_t>(vtxOffset + ImCmd.VtxOffset));
            }
            idxOffset += cmdList->IdxBuffer.Size;
            vtxOffset += cmdList->VtxBuffer.Size;
        }

        cmdPopMarker(cmd);
    }

    void ImGuiRenderer::CreateNewPipeline(const Framebuffer& framebuffer)
    {
        CHECK(framebuffer.Color[0].Texture, "There should be at least 1 valid texture in the framebuffer");

        const uint32_t nonLinearColorSpace = Context->GetSwapchainColorSpace() == ColorSpace::SRGB_NonLinear ? 1u : 0u;
        static_assert(EOS_MAX_COLOR_ATTACHMENTS == 8, "Update all color attachments below");

        const RenderPipelineDescription renderPipelineDesc
        {
            .VertexShader = VertexShader,
            .FragmentShader = FragmentShader,
            .SpecInfo =
            {
                .Entries = {{.ID = 0, .Size = sizeof(nonLinearColorSpace)}},
                .Data = &nonLinearColorSpace,
                .DataSize = sizeof(nonLinearColorSpace),
            },

            .ColorAttachments =
            {{
                .ColorFormat = Context->GetFormat(framebuffer.Color[0].Texture),
                .BlendEnabled = true,
                .SrcRGBBlendFactor = EOS::BlendFactor::SrcAlpha,
                .DstRGBBlendFactor = EOS::BlendFactor::OneMinusSrcAlpha,
            },
                {.ColorFormat = framebuffer.Color[1].Texture ? Context->GetFormat(framebuffer.Color[1].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[2].Texture ? Context->GetFormat(framebuffer.Color[2].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[3].Texture ? Context->GetFormat(framebuffer.Color[3].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[4].Texture ? Context->GetFormat(framebuffer.Color[4].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[5].Texture ? Context->GetFormat(framebuffer.Color[5].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[6].Texture ? Context->GetFormat(framebuffer.Color[6].Texture) : EOS::Format::Invalid},
                {.ColorFormat = framebuffer.Color[7].Texture ? Context->GetFormat(framebuffer.Color[7].Texture) : EOS::Format::Invalid},
            },
            .DepthFormat = framebuffer.DepthStencil.Texture ? Context->GetFormat(framebuffer.DepthStencil.Texture) : EOS::Format::Invalid,
            .PipelineCullMode = CullMode::None,
            .DebugName = "ImGui Render Pipeline"
        };


        RenderPipeline = Context->CreateRenderPipeline(renderPipelineDesc);
    }
}
