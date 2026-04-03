#include "ImGuiRenderer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "ShaderTools/shaderCompiler.h"

namespace EOS
{
    namespace
    {
        constexpr uint64_t kImGuiTextureIDEncodedBit = 1ull << 63;
        constexpr uint64_t kImGuiTextureIDTextureMask = 0x00000000ffffffffull;
        constexpr uint64_t kImGuiTextureIDLayerMask = 0x0fffffffull;
        constexpr uint32_t kImGuiTextureIDLayerShift = 32;
        constexpr uint32_t kImGuiTextureIDViewShift = 60;

        struct DecodedImGuiTextureID final
        {
            uint32_t TextureID = 0;
            uint32_t Layer = 0;
            ImGuiTextureView View = ImGuiTextureView::Texture2D;
        };

        [[nodiscard]] DecodedImGuiTextureID DecodeImGuiTextureID(const uint64_t packedTextureID)
        {
            if ((packedTextureID & kImGuiTextureIDEncodedBit) == 0)
            {
                return {.TextureID = static_cast<uint32_t>(packedTextureID)};
            }

            const uint32_t textureID = static_cast<uint32_t>(packedTextureID & kImGuiTextureIDTextureMask);
            const uint32_t layer = static_cast<uint32_t>((packedTextureID >> kImGuiTextureIDLayerShift) & kImGuiTextureIDLayerMask);
            const uint32_t view = static_cast<uint32_t>((packedTextureID >> kImGuiTextureIDViewShift) & 0x7ull);
            const ImGuiTextureView textureView = view == static_cast<uint32_t>(ImGuiTextureView::Texture2DArray)
                ? ImGuiTextureView::Texture2DArray
                : ImGuiTextureView::Texture2D;

            return
            {
                .TextureID = textureID,
                .Layer = layer,
                .View = textureView,
            };
        }
    }

    uint64_t MakeImGuiTextureID(const TextureHandle texture, const uint32_t layer, const ImGuiTextureView view)
    {
        const uint64_t textureID = texture.Index();
        if (view == ImGuiTextureView::Texture2D && layer == 0)
        {
            return textureID;
        }

        return
            kImGuiTextureIDEncodedBit |
            ((static_cast<uint64_t>(view) & 0x7ull) << kImGuiTextureIDViewShift) |
            ((static_cast<uint64_t>(layer) & kImGuiTextureIDLayerMask) << kImGuiTextureIDLayerShift) |
            textureID;
    }

    ImGuiRenderer::ImGuiRenderer(IContext* context, const Window& window, const char* defaultFont, float fontSize)
    :Context(context)
    {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "imgui-EOS";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImGui_ImplGlfw_InitForOther(window.GlfwWindow, window.GlfwWindow ? true : false);

        CurrentFont = defaultFont ? defaultFont : "";
        BaseFontSize = fontSize;
        SetFont(defaultFont, fontSize);

        constexpr SamplerDescription samplerDesc
        {
            .wrapU = SamplerWrap::Clamp,
            .wrapV = SamplerWrap::Clamp,
            .wrapW = SamplerWrap::Clamp,
            .debugName = "Imgui Sampler",
        };
        Sampler = context->CreateSampler(samplerDesc);

        VertexShader    = context->CreateShaderModule("imgui", ShaderStage::Vertex);
        FragmentShader  = context->CreateShaderModule("imgui", ShaderStage::Fragment);
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        const ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexRef = ImTextureRef();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiRenderer::SetFont(const char* defaultFont, float fontSize)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

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
        io.Fonts->TexID = MakeImGuiTextureID(FontTexture);
        io.FontDefault = font;
    }

    void ImGuiRenderer::SetScale(float scale)
    {
        PendingScale = scale;
    }

    void ImGuiRenderer::SetScaleInternal()
    {
        //Reset Styling
        ImGui::GetStyle() = ImGuiStyle();

        //Set Scale
        Scale = PendingScale;
        ImGui::GetStyle().ScaleAllSizes(Scale);
        ImGui::GetStyle().FontScaleMain = Scale;

        // Rebuild font at target size
        SetFont(CurrentFont, BaseFontSize * Scale);
    }

    void ImGuiRenderer::BeginFrame(ICommandBuffer& cmd)
    {
        if (Scale != PendingScale)
        {
            SetScaleInternal();
        }

        constexpr RenderPass renderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Load, } },
        };

        Framebuffer framebuffer
        {
            .Color = {{.Texture = Context->GetSwapChainTexture()}},
            .DebugName = "ImGui framebuffer"
        };


        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        if (RenderPipeline.Empty()) CreateNewPipeline(framebuffer);

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        cmdPushMarker(cmd, "GUI", 0xff11ff01);
        cmdBeginRendering(cmd, renderPass, framebuffer);
    }

    void ImGuiRenderer::EndFrame(ICommandBuffer& cmd)
    {
        static_assert(sizeof(ImDrawIdx) == 2);

        ImGui::EndFrame();
        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
        const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
        if (framebufferWidth <= 0 || framebufferHeight <= 0 || drawData->CmdListsCount == 0)  return;

        cmdSetDepthState(cmd, {});
        cmdBindViewport(cmd, {.X = 0.0f, .Y = 0.0f, .Width = framebufferWidth, .Height = framebufferHeight});

        auto& [VertexBuffer, IndexBuffer, allocatedIndices, allocatedVertices] = Drawables[FrameIndex];
        FrameIndex = (FrameIndex + 1) % ARRAY_COUNT(Drawables);

        //Create index buffer
        if (allocatedIndices < drawData->TotalIdxCount)
        {
            const BufferDescription indexBufferDesc
            {
                .Usage = BufferUsageFlags::Index,
                .Storage = StorageType::HostVisible,
                .Size = drawData->TotalIdxCount * sizeof(ImDrawIdx),
                .DebugName = "Imgui Index Buffer"
            };

            IndexBuffer = Context->CreateBuffer(indexBufferDesc);
            allocatedIndices = drawData->TotalIdxCount;
        }

        //Create Vertex Buffer
        if (allocatedVertices < drawData->TotalVtxCount)
        {
            const BufferDescription vertexBufferDesc
            {
                .Usage = BufferUsageFlags::StorageFlag,
                .Storage = StorageType::HostVisible,
                .Size = drawData->TotalVtxCount * sizeof(ImDrawVert),
                .DebugName = "Imgui Vertex Buffer"
            };

            VertexBuffer = Context->CreateBuffer(vertexBufferDesc);
            allocatedVertices = drawData->TotalVtxCount;
        }

        //Upload vertex/index buffers
        {
            ImDrawVert* vertexBufferPtr = reinterpret_cast<ImDrawVert*>(Context->GetMappedPtr(VertexBuffer));
            uint16_t* indexBufferPtr = reinterpret_cast<uint16_t*>(Context->GetMappedPtr(IndexBuffer));

            for (const ImDrawList* cmdList : drawData->CmdLists)
            {
                memcpy(vertexBufferPtr, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
                memcpy(indexBufferPtr, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

                vertexBufferPtr += cmdList->VtxBuffer.Size;
                indexBufferPtr += cmdList->IdxBuffer.Size;
            }

            Context->FlushMappedMemory(VertexBuffer, drawData->TotalVtxCount * sizeof(ImDrawVert));
            Context->FlushMappedMemory(IndexBuffer, drawData->TotalIdxCount * sizeof(ImDrawIdx));
        }

        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;

        cmdBindIndexBuffer(cmd, IndexBuffer, IndexFormat::UI16);
        cmdBindRenderPipeline(cmd, RenderPipeline);

        const float left = drawData->DisplayPos.x;
        const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
        const float top = drawData->DisplayPos.y;
        const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;

        const ImVec2 clipPosition = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;

        for (const ImDrawList* cmdList : drawData->CmdLists)
        {
            const uint64_t listVertexBufferPtr = Context->GetGPUAddress(VertexBuffer) + vertexOffset * sizeof(ImDrawVert);  // ← offset the BDA pointer
            for (int cmd_i{}; cmd_i < cmdList->CmdBuffer.Size; ++cmd_i)
            {
                const ImDrawCmd ImCmd = cmdList->CmdBuffer[cmd_i];
                CHECK(ImCmd.UserCallback == nullptr, "The user callback of the imgui draw is set.");

                ImVec2 clipMin((ImCmd.ClipRect.x - clipPosition.x) * clipScale.x, (ImCmd.ClipRect.y - clipPosition.y) * clipScale.y);
                ImVec2 clipMax((ImCmd.ClipRect.z - clipPosition.x) * clipScale.x, (ImCmd.ClipRect.w - clipPosition.y) * clipScale.y);

                //Check the clip regions -> scissor out
                if (clipMin.x < 0.0f) clipMin.x = 0.0f;
                if (clipMin.y < 0.0f) clipMin.y = 0.0f;
                if (clipMax.x > framebufferWidth ) clipMax.x = framebufferWidth;
                if (clipMax.y > framebufferHeight) clipMax.y = framebufferHeight;
                if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) continue;

                const DecodedImGuiTextureID textureData = DecodeImGuiTextureID(static_cast<uint64_t>(ImCmd.GetTexID()));
                BindData bindData
                {
                    .LRTB               = {left, right, top, bottom},
                    .vertexBufferPtr    = listVertexBufferPtr,
                    .textureId          = textureData.TextureID,
                    .samplerId          = Sampler.Index(),
                    .textureLayer       = textureData.Layer,
                    .textureView        = static_cast<uint32_t>(textureData.View),
                };

                cmdPushConstants(cmd, bindData);
                cmdBindScissorRect(cmd, {static_cast<uint32_t>(clipMin.x), static_cast<uint32_t>(clipMin.y), static_cast<uint32_t>(clipMax.x - clipMin.x), static_cast<uint32_t>(clipMax.y - clipMin.y)});
                cmdDrawIndexed(cmd, ImCmd.ElemCount, 1u, indexOffset + ImCmd.IdxOffset, static_cast<int32_t>(ImCmd.VtxOffset));
            }
            indexOffset += cmdList->IdxBuffer.Size;
            vertexOffset += cmdList->VtxBuffer.Size;
        }

        cmdEndRendering(cmd);
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
                .SrcRGBBlendFactor = BlendFactor::SrcAlpha,
                .DstRGBBlendFactor = BlendFactor::OneMinusSrcAlpha,
            },
                {.ColorFormat = framebuffer.Color[1].Texture ? Context->GetFormat(framebuffer.Color[1].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[2].Texture ? Context->GetFormat(framebuffer.Color[2].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[3].Texture ? Context->GetFormat(framebuffer.Color[3].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[4].Texture ? Context->GetFormat(framebuffer.Color[4].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[5].Texture ? Context->GetFormat(framebuffer.Color[5].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[6].Texture ? Context->GetFormat(framebuffer.Color[6].Texture) : Invalid},
                {.ColorFormat = framebuffer.Color[7].Texture ? Context->GetFormat(framebuffer.Color[7].Texture) : Invalid},
            },
            .DepthFormat = framebuffer.DepthStencil.Texture ? Context->GetFormat(framebuffer.DepthStencil.Texture) : Invalid,
            .PipelineCullMode = CullMode::None,
            .DebugName = "ImGui Render Pipeline"
        };

        RenderPipeline = Context->CreateRenderPipeline(renderPipelineDesc);
    }
}
