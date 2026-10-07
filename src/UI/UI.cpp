#include "UI.h"
#include <algorithm>
#include <cstdarg>
#include <string>

#include "formatNames.h"
#include "renderGraphFile.h"
#include ".generated/eos/preview.h"

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

    void PushID([[maybe_unused]] const char* id)
    {
#if defined(EOS_USE_IMGUI)
        ImGui::PushID(id);
#endif
    }

    void PopID()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::PopID();
#endif
    }

    void Indent()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::Indent();
#endif
    }

    void Unindent()
    {
#if defined(EOS_USE_IMGUI)
        ImGui::Unindent();
#endif
    }

    bool SliderFloat2([[maybe_unused]] const char* label, [[maybe_unused]] float* values, [[maybe_unused]] const float minimum, [[maybe_unused]] const float maximum)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::SliderFloat2(label, values, minimum, maximum);
#else
        return false;
#endif
    }

    bool SliderFloat4([[maybe_unused]] const char* label, [[maybe_unused]] float* values, [[maybe_unused]] const float minimum, [[maybe_unused]] const float maximum)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::SliderFloat4(label, values, minimum, maximum);
#else
        return false;
#endif
    }

    bool DragFloat([[maybe_unused]] const char* label, [[maybe_unused]] float* value)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::DragFloat(label, value, 0.01f);
#else
        return false;
#endif
    }

    bool DragFloat4([[maybe_unused]] const char* label, [[maybe_unused]] float* values)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::DragFloat4(label, values, 0.01f);
#else
        return false;
#endif
    }

    bool DragInt([[maybe_unused]] const char* label, [[maybe_unused]] int* value)
    {
#if defined(EOS_USE_IMGUI)
        return ImGui::DragInt(label, value);
#else
        return false;
#endif
    }

    void GraphFileProperties(GraphFile& file)
    {
        for (uint32_t passIndex = 0; passIndex < file.GetPassCount(); ++passIndex)
        {
            const GraphFilePass pass = file.GetPass(passIndex);
            PushID(pass.Name);
            Checkbox(pass.Name, pass.Enabled);

            Indent();
            for (uint32_t propertyIndex = 0; propertyIndex < pass.PropertyCount; ++propertyIndex)
            {
                const GraphFileProperty property = file.GetProperty(passIndex, propertyIndex);
                PropertyValue& value = *property.Value;
                const bool ranged = property.Min < property.Max;
                float* components = &value.Float.x;

                switch (property.Type)
                {
                    case PropertyType::Bool:
                        Checkbox(property.Name, &value.Bool);
                        break;
                    case PropertyType::Int:
                        if (ranged) SliderInt(property.Name, &value.Int, static_cast<int>(property.Min), static_cast<int>(property.Max));
                        else DragInt(property.Name, &value.Int);
                        break;
                    case PropertyType::Float:
                        if (ranged) SliderFloat(property.Name, components, property.Min, property.Max);
                        else DragFloat(property.Name, components);
                        break;
                    case PropertyType::Float2:
                        if (ranged) SliderFloat2(property.Name, components, property.Min, property.Max);
                        else DragFloat2(property.Name, components);
                        break;
                    case PropertyType::Float3:
                        if (ranged) SliderFloat3(property.Name, components, property.Min, property.Max);
                        else DragFloat3(property.Name, components);
                        break;
                    case PropertyType::Float4:
                        if (ranged) SliderFloat4(property.Name, components, property.Min, property.Max);
                        else DragFloat4(property.Name, components);
                        break;
                    case PropertyType::Choice:
                        Combo(property.Name, &value.Int, property.Choices.data(), static_cast<int>(property.Choices.size()));
                        break;
                    case PropertyType::String:
                        Text("%s: %s", property.Name, value.String);
                        break;
                }
            }
            Unindent();
            PopID();
        }
    }

    // -------------------------------------------------------------------------------------------------------------------
    // GraphFilePanel

    namespace
    {
        // Formats a float sampler cannot read.
        [[nodiscard]] bool IsIntegerFormat(Format format)
        {
            return format == Format::R_UI16 || format == Format::R_UI32 || format == Format::RG_UI16 || format == Format::RG_UI32 || format == Format::RGBA_UI32;
        }

        [[nodiscard]] bool IsSingleChannelFormat(Format format)
        {
            return IsDepthFormat(format) || format == Format::R_UN8 || format == Format::R_UN16 || format == Format::R_F16 || format == Format::R_F32;
        }

        constexpr uint32_t kPreviewWidth = 384;
    }

    struct GraphFilePanel::Implementation final
    {
        Implementation(IContext* context, GraphFile& file) : Context(context), File(file) {}

        IContext* Context;
        GraphFile& File;

        std::string Previewed;                  // "Pass.pin"; empty for none
        int Layer = 0;
        float Range[2]{0.0f, 1.0f};

        // What the last preview pass found.
        bool Drawn = false;
        bool Integer = false;
        uint32_t LayerCount = 1;

        TextureHolder Image;
        Dimensions ImageSize{0, 0, 0};
        ShaderProgramHolder Program;            // eos.preview
        ShaderProgramHolder Fullscreen;         // eos.fullscreen's vertex shader
        RenderPipelineHolder Pipeline;
        SamplerHolder Sampler;
    };

    GraphFilePanel::GraphFilePanel(IContext* context, GraphFile& file)
    : Impl(std::make_unique<Implementation>(context, file))
    {
    }

    GraphFilePanel::~GraphFilePanel() = default;

    GraphTexture GraphFilePanel::AddPreviewPass([[maybe_unused]] RenderGraph& graph)
    {
#if defined(EOS_USE_IMGUI)
        Implementation& panel = *Impl;
        panel.Drawn = false;
        if (panel.Previewed.empty()) return {};

        // Nothing when the pass did not run this frame; the swapchain cannot be sampled.
        const GraphTexture source = panel.File.GetTexture(panel.Previewed);
        if (!source.Valid() || graph.IsSwapchain(source)) return {};

        const GraphTextureDescription& description = graph.GetDescription(source);
        panel.Integer = IsIntegerFormat(description.TextureFormat);
        if (panel.Integer) return {};

        const bool array = description.Type == ImageType::Image_2D_Array;
        panel.LayerCount = array ? description.NumberOfLayers : 1;
        panel.Layer = std::clamp(panel.Layer, 0, static_cast<int>(panel.LayerCount) - 1);

        const Dimensions size = graph.GetSize(source);
        const uint32_t height = std::clamp(kPreviewWidth * size.Height / std::max(size.Width, 1u), 1u, 2 * kPreviewWidth);
        if (!panel.Image.Valid() || panel.ImageSize.Width != kPreviewWidth || panel.ImageSize.Height != height)
        {
            panel.ImageSize = {kPreviewWidth, height, 1};
            panel.Image = panel.Context->CreateTexture(
            {
                .TextureFormat = Format::RGBA_UN8,
                .TextureDimensions = panel.ImageSize,
                .Usage = TextureUsageFlags::Attachment | TextureUsageFlags::Sampled,
                .DebugName = "Graph Preview",
            });
        }
        if (panel.Pipeline.Empty())
        {
            panel.Program = panel.Context->CreateShaderProgram({.Module = "eos.preview"});
            panel.Fullscreen = panel.Context->CreateShaderProgram({.Module = "eos.fullscreen"});
            panel.Pipeline = panel.Context->CreateRenderPipeline(
            {
                .VertexShader = {panel.Fullscreen},
                .FragmentShader = {panel.Program},
                .ColorAttachments = {{.ColorFormat = Format::RGBA_UN8}},
                .PipelineCullMode = CullMode::None,
                .DebugName = "Graph Preview",
            });
            panel.Sampler = panel.Context->CreateSampler({.wrapU = SamplerWrap::Clamp, .wrapV = SamplerWrap::Clamp, .wrapW = SamplerWrap::Clamp, .maxAnisotropic = 0, .debugName = "Graph Preview"});
        }

        const GraphTexture image = graph.ImportTexture(panel.Image, "Graph Preview");
        PreviewConstants constants
        {
            .samplerState = panel.Sampler,
            .layer = static_cast<uint32_t>(panel.Layer),
            .grey = IsSingleChannelFormat(description.TextureFormat) ? 1u : 0u,
            .minimum = panel.Range[0],
            .maximum = panel.Range[1],
        };

        graph.AddRasterPass("Graph Preview").Color(Clear(image)).Sample(source).Execute([&panel, source, array, constants](PassContext& pass) mutable
        {
            (array ? constants.textureArray : constants.texture) = pass.Descriptor(source);
            cmdBindRenderPipeline(pass.Cmd, panel.Pipeline);
            cmdPushConstants(pass.Cmd, constants);
            cmdDraw(pass.Cmd, 3);
        });

        panel.Drawn = true;
        return image;
#else
        return {};
#endif
    }

    void GraphFilePanel::Declare()
    {
        Implementation& panel = *Impl;

#if defined(EOS_USE_IMGUI)
        // The preview first, so it stays in view above a long list of passes.
        if (ImGui::BeginCombo("Preview", panel.Previewed.empty() ? "None" : panel.Previewed.c_str()))
        {
            if (ImGui::Selectable("None", panel.Previewed.empty())) panel.Previewed.clear();

            // Every texture a pass writes.
            for (uint32_t passIndex = 0; passIndex < panel.File.GetPassCount(); ++passIndex)
            {
                const GraphFilePass pass = panel.File.GetPass(passIndex);
                for (uint32_t pinIndex = 0; pinIndex < pass.PinCount; ++pinIndex)
                {
                    const GraphFilePin pin = panel.File.GetPin(passIndex, pinIndex);
                    if (pin.Direction == PinDirection::Input || IsBufferUsage(pin.Usage)) continue;

                    const std::string name = std::string(pass.Name) + "." + pin.Name;
                    if (ImGui::Selectable(name.c_str(), name == panel.Previewed)) panel.Previewed = name;
                }
            }
            ImGui::EndCombo();
        }

        if (!panel.Previewed.empty())
        {
            if (panel.Integer) Text("An integer format cannot be shown");
            else if (!panel.Drawn) Text("Not written this frame (disabled, or the swapchain)");
            else
            {
                if (panel.LayerCount > 1) SliderInt("Layer", &panel.Layer, 0, static_cast<int>(panel.LayerCount) - 1);
                ImGui::DragFloatRange2("Range", &panel.Range[0], &panel.Range[1], 0.01f);
                // As wide as the panel, at most at its own size.
                const float width = std::min(ImGui::GetContentRegionAvail().x, static_cast<float>(panel.ImageSize.Width));
                Image(MakeTextureID(panel.Image), width, width * static_cast<float>(panel.ImageSize.Height) / static_cast<float>(panel.ImageSize.Width));
            }
        }
        Separator();
#endif

        GraphFileProperties(panel.File);
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
