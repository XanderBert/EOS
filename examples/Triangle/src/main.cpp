#include "EOS.h"
#include "renderGraph.h"

struct Resources final
{
    EOS::ShaderProgramHolder Shader;
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
    Handles.Shader = context->CreateShaderProgram({.Module = "triangle"});

    EOS::SamplerDescription samplerDescription
    {
        .mipLodMax = EOS_MAX_MIP_LEVELS,
        .maxAnisotropic = 0,
        .debugName = "Linear Sampler",
    };
    Handles.Sampler = context->CreateSampler(samplerDescription);

    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexShader = {Handles.Shader, "vertexMain"},
        .FragmentShader = {Handles.Shader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipeline = context->CreateRenderPipeline(renderPipelineDescription);

    std::unique_ptr<EOS::RenderGraph> graph = std::make_unique<EOS::RenderGraph>(context.get());

    while (!window->ShouldClose())
    {
        window->Poll();

        const EOS::GraphTexture backbuffer = graph->ImportSwapchain();
        graph->AddRasterPass("Triangle").Color(EOS::Clear(backbuffer, {0.36f, 0.4f, 1.0f, 0.28f})).Execute([](EOS::PassContext& pass)
        {
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipeline);
            cmdDraw(pass.Cmd, 3);
        });
        graph->Execute();
    }

    graph = nullptr;
    Handles = {};

    return 0;
}