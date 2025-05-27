#include "window.h"
#include "EOS.h"

namespace EOS
{
    GLFWwindow*  Window::InitWindow(ContextCreationDescription& contextDescription)
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
            return nullptr;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        // Determine if we're in fullscreen mode
        const bool fullscreen = !contextDescription.Width || !contextDescription.Height;
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
            w = contextDescription.Width;
            h = contextDescription.Height;
        }

        // Create the window
        GLFWwindow* window = glfwCreateWindow(w, h, contextDescription.ApplicationName , fullscreen ? monitor : nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            contextDescription.Window = nullptr;
            contextDescription.Display = nullptr;
            return nullptr;
        }

        // Position the window in fullscreen mode
        if (fullscreen)
        {
            glfwSetWindowPos(window, x, y);
        }

        // Get the actual window size and store it
        glfwGetWindowSize(window, &w, &h);
        contextDescription.Width  = w;
        contextDescription.Height = h;

        // Setup the key callback
        glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int, int action, int)
        {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        });

#if defined(EOS_PLATFORM_WAYLAND)
        contextDescription.Window     = static_cast<void*>(glfwGetWaylandWindow(window));
        contextDescription.Display    = static_cast<void*>(glfwGetWaylandDisplay());
#elif defined(EOS_PLATFORM_X11)
        contextDescription.window     = reinterpret_cast<void*>(glfwGetX11Window(window));
        contextDescription.display    = static_cast<void*>(glfwGetX11Display());
#elif defined(EOS_PLATFORM_WIN32)
        contextDescription.window     = static_cast<void*>(glfwGetWin32Window(window));
        contextDescription.display    = nullptr;  // Not used on Windows
#endif


        return window;
    }
}

