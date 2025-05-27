#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Window",
    };

    GLFWwindow* window = EOS::Window::InitWindow(contextDescr);
    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapChain(contextDescr);
    std::unique_ptr<EOS::ShaderCompiler> shaderCompiler = EOS::CreateShaderCompiler("./");

    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(context, shaderCompiler, "triangleVert");
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(context, shaderCompiler, "triangleFrag");

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription triangleDescription
    {
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}}
    };

    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = context->CreateRenderPipeline(triangleDescription);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        glfwGetFramebufferSize(window, &contextDescr.Width, &contextDescr.Height);
        if (!contextDescr.Width || !contextDescr.Height)
        {
            continue; // Or sleep
        }

        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::Present}});


        EOS::Framebuffer framebuffer = {.Color = {{.Texture = context->GetSwapChainTexture()}}};
        EOS::RenderPass renderPass{ .Color = { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } }};
        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdPushMarker(cmdBuffer, "Render Triangle", 0xff0000ff);
            cmdDraw(cmdBuffer, 3);
            cmdPopMarker(cmdBuffer);
        cmdEndRendering(cmdBuffer);

        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    EOS::Window::DestroyWindow(window);
    return 0;
}