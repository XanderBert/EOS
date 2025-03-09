#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
    //TODO: Add support for X11
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#endif

#include <GLFW/glfw3native.h>
#include <stdio.h>



namespace EOS
{
    [[nodiscard]] inline GLFWwindow* InitWindow(const char* windowTitle, uint32_t& outWidth, uint32_t& outHeight)
    {
        /// Setup the error callback
        glfwSetErrorCallback([](int error, const char* message)
        {
            printf("GLFW: ERROR (%i): %s\n", error, message);
        });

        // Initialize GLFW
        if (!glfwInit())
        {
            return nullptr;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        // Determine if we're in fullscreen mode
        const bool fullscreen = !outWidth || !outHeight;
        glfwWindowHint(GLFW_RESIZABLE, fullscreen ? GLFW_FALSE : GLFW_TRUE);

        // Get the primary monitor and its video mode
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        // Calculate window dimensions and position
        int x = 0, y = 0;
        int w = mode->width;
        int h = mode->height;

        if (fullscreen)
        {
            // Adjust for the taskbar in fullscreen mode
            glfwGetMonitorWorkarea(monitor, &x, &y, &w, &h);
        }
        else
        {
            // Use the provided dimensions for windowed mode
            w = static_cast<int>(outWidth);
            h = static_cast<int>(outHeight);
        }

        // Create the window
        GLFWwindow* window = glfwCreateWindow(w, h, windowTitle, fullscreen ? monitor : nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            return nullptr;
        }

        // Position the window in fullscreen mode
        if (fullscreen)
        {
            glfwSetWindowPos(window, x, y);
        }

        // Get the actual window size and store it
        glfwGetWindowSize(window, &w, &h);
        outWidth  = static_cast<uint32_t>(w);
        outHeight = static_cast<uint32_t>(h);

        // Setup the key callback
        glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int, int action, int)
        {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        });

        return window;
    }

    inline void DestroyWindow(GLFWwindow* window)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}