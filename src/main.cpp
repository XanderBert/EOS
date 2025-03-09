#include <iostream>
#include <window.h>

int main()
{
    uint32_t width{}, height{};
    auto* window = EOS::InitWindow("Hello, World!", width, height);
    if (!window){ std::cout << "Failed to create window" << std::endl;}


    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }



    EOS::DestroyWindow(window);
    return 0;
}