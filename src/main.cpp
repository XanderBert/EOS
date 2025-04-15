#include "EOS.h"
#include "logger.h"

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .config =
        {
            .enableValidationLayers = true
        },
        .preferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .applicationName        = "EOS - Window",
    };

    uint32_t width{}, height{};
    GLFWwindow* window = EOS::Window::InitWindow(contextDescr, width, height);
    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapChain(contextDescr);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    EOS::Window::DestroyWindow(window);
    return 0;
}
