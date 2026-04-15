#include "EOS.h"

struct Resources final
{
    EOS::Holder<EOS::ShaderModuleHandle> ShaderHandleVert;
    EOS::Holder<EOS::ShaderModuleHandle> ShaderHandleFrag;
    EOS::Holder<EOS::SamplerHandle> Sampler;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipeline;
};

Resources Handles;

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Render Triangle",
    };

    std::unique_ptr<EOS::Window> window = std::make_unique<EOS::Window>(contextDescr);
    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapChain(contextDescr);
    Handles.ShaderHandleVert = context->CreateShaderModule("triangle", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleFrag = context->CreateShaderModule("triangle", EOS::ShaderStage::Fragment);

    EOS::SamplerDescription samplerDescription
    {
        .mipLodMax = EOS_MAX_MIP_LEVELS,
        .maxAnisotropic = 0,
        .debugName = "Linear Sampler",
    };
    Handles.Sampler = context->CreateSampler(samplerDescription);

    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexShader = Handles.ShaderHandleVert,
        .FragmentShader = Handles.ShaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipeline = context->CreateRenderPipeline(renderPipelineDescription);

    bool AllowStartupFrame = true;

    while (!window->ShouldClose())
    {
        window->Poll();

        if (!window->IsFocused() && !AllowStartupFrame) continue;
        AllowStartupFrame = false;


        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        EOS::Framebuffer framebuffer =
        {
            .Color = {{.Texture = context->GetSwapChainTexture()}},
            .DebugName = "Triangle Framebuffer",
        };
        EOS::RenderPass renderPass{ .Color = { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } }};
        cmdPipelineBarrier(cmdBuffer, {},{{ context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget }});

        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Triangle", 0xff0000ff);
            cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipeline);
            cmdDraw(cmdBuffer, 3);
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    Handles = {};

    return 0;
}