#include <iostream>
#include "EOS.h"

int main()
{
    uint32_t width{}, height{};
    GLFWwindow* window = nullptr;//EOS::InitWindow("Hello, World!", width, height);
    //if (!window){ std::cout << "Failed to create window" << std::endl;}

    EOS::ContextCreationDescription contextDescr
    {
        .config =
     {
            .enableValidationLayers = true
        },

        .window                 = window,
        .preferredHardwareType  = EOS::HardwareDeviceType::Discrete
    };

    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapchain(contextDescr);


    //while (!glfwWindowShouldClose(window))
    //{
    //    glfwPollEvents();
    //}

    //EOS::DestroyWindow(window);
    return 0;
}