#include "window.h"
#include "EOS.h"

namespace EOS
{
    Window::Window(ContextCreationDescription& contextDescription)
    {
        /// Setup the error callback
        glfwSetErrorCallback([](int error, const char* message)
        {
            printf("GLFW: ERROR (%i): %s\n", error, message);
        });

        // Initialize GLFW
        if (!glfwInit())
        {
            contextDescription.Window = nullptr;
            contextDescription.Display = nullptr;
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if defined(EOS_PLATFORM_WAYLAND)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#elif defined(EOS_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#elif defined(EOS_PLATFORM_WIN32)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#endif

        // Determine if we're in fullscreen mode
        const bool fullscreen = !contextDescription.Width || !contextDescription.Height;
        glfwWindowHint(GLFW_RESIZABLE, fullscreen ? GLFW_FALSE : GLFW_TRUE);

        // Get the primary monitor and its video mode
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        // Calculate window dimensions and position
        int x = 0, y = 0;
        Width = mode->width;
        Height = mode->height;

        if (fullscreen)
        {
            // Adjust for the taskbar in fullscreen mode
            glfwGetMonitorWorkarea(monitor, &x, &y, &Width, &Height);
        }
        else
        {
            // Use the provided dimensions for windowed mode
            Width = contextDescription.Width;
            Height = contextDescription.Height;
        }

        // Create the window
        GlfwWindow = glfwCreateWindow(Width, Height, contextDescription.ApplicationName , fullscreen ? monitor : nullptr, nullptr);
        if (!GlfwWindow)
        {
            glfwTerminate();
            contextDescription.Window = nullptr;
            contextDescription.Display = nullptr;
            return;
        }

        // Position the window in fullscreen mode
        if (fullscreen)
        {
            glfwSetWindowPos(GlfwWindow, x, y);
        }

        // Get the actual window size and store it
        glfwGetWindowSize(GlfwWindow, &Width, &Height);
        contextDescription.Width  = Width;
        contextDescription.Height = Height;

        // Setup the key callback
        glfwSetKeyCallback(GlfwWindow, [](GLFWwindow* window, int key, int, int action, int)
        {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        });

#if defined(EOS_PLATFORM_WAYLAND)
        contextDescription.Window     = static_cast<void*>(glfwGetWaylandWindow(GlfwWindow));
        contextDescription.Display    = static_cast<void*>(glfwGetWaylandDisplay());
#elif defined(EOS_PLATFORM_X11)
        contextDescription.Window     = reinterpret_cast<void*>(glfwGetX11Window(GlfwWindow));
        contextDescription.Display    = static_cast<void*>(glfwGetX11Display());
#elif defined(EOS_PLATFORM_WIN32)
        contextDescription.Window     = static_cast<void*>(glfwGetWin32Window(GlfwWindow));
        contextDescription.Display    = nullptr;  // Not used on Windows
#endif
    }

    Window::~Window()
    {
        glfwDestroyWindow(GlfwWindow);
        glfwTerminate();
    }

    void Window::Poll()
    {
        glfwPollEvents();
    }

    bool Window::ShouldClose() const
    {
        return glfwWindowShouldClose(GlfwWindow);
    }

    bool Window::IsFocused()
    {
        glfwGetFramebufferSize(GlfwWindow, &Width, &Height);

        return Width || Height;
    }
}

