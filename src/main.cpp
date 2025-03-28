#include "EOS.h"

int main()
{
    //TODO Wrap window and fill in display settings for Context Creation
    //The end user should not deal with third party functions or types
    uint32_t width{}, height{};
    GLFWwindow* window = EOS::InitWindow("EOS", width, height);
    if (!window){ printf("Failed to Create a window.\n"); }

    EOS::ContextCreationDescription contextDescr
    {
        .config =
     {
            .enableValidationLayers = true
        },
        .window                 = static_cast<void*>(window),
        .display                = nullptr,
        .preferredHardwareType  = EOS::HardwareDeviceType::Discrete
    };

    //TODO Should not be handled in main but in EOS (or a window wrapper, the window wrapper can hold these values instead of manually specifying them)
#if defined(EOS_PLATFORM_WAYLAND)
    contextDescr.window     = static_cast<void*>(glfwGetWaylandWindow(window));
    contextDescr.display    = static_cast<void*>(glfwGetWaylandDisplay());
#endif

    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapChain(contextDescr);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    EOS::DestroyWindow(window);
    return 0;
}