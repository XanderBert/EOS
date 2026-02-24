#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"
#include "utils.h"

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
    std::unique_ptr<EOS::ShaderCompiler> shaderCompiler = EOS::CreateShaderCompiler("./");
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(context, shaderCompiler, "triangle", EOS::ShaderStage::Vertex);
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(context, shaderCompiler, "triangle", EOS::ShaderStage::Fragment);

    EOS::SamplerDescription samplerDescription
    {
        .mipLodMax = EOS_MAX_MIP_LEVELS,
        .maxAnisotropic = 0,
        .debugName = "Linear Sampler",
    };
    EOS::Holder<EOS::SamplerHandle> sampler = context->CreateSampler(samplerDescription);

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},
        .DebugName = "Basic Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = context->CreateRenderPipeline(renderPipelineDescription);

    while (!window->ShouldClose())
    {
        window->Poll();
        if (!window->IsFocused()) continue;


        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        EOS::Framebuffer framebuffer = {.Color = {{.Texture = context->GetSwapChainTexture()}}};
        EOS::RenderPass renderPass{ .Color = { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } }};
        cmdPipelineBarrier(cmdBuffer, {},{{ context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget }});

        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Triangle", 0xff0000ff);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdDraw(cmdBuffer, 3);
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    return 0;
}