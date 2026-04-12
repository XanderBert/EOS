#include "window.h"
#include "EOS.h"

#include <algorithm>

namespace EOS
{
    Window::Window(ContextCreationDescription& contextDescription)
    {
        /// Setup the error callback
        glfwSetErrorCallback([](int error, const char* message)
        {
            printf("%s", fmt::format("GLFW: {}, {}", error, message).c_str());
        });

#if defined(EOS_PLATFORM_WAYLAND) || defined(EOS_PLATFORM_X11)
        char* displayEnv = getenv("WAYLAND_DISPLAY");
        if (displayEnv && strlen(displayEnv) > 0)
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        else
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#elif defined(EOS_PLATFORM_WINDOWS)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#endif

        // Initialize GLFW
        if (!glfwInit())
        {
            contextDescription.Window = nullptr;
            return;
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
            return;
        }

        // Position the window in fullscreen mode
        if (fullscreen)
        {
            glfwSetWindowPos(GlfwWindow, x, y);
        }

#if defined(EOS_PLATFORM_WINDOWS)
        // Get the actual window size and store it
        glfwGetFramebufferSize(GlfwWindow, &Width, &Height);
#endif

        contextDescription.Width  = Width;
        contextDescription.Height = Height;

        glfwSetWindowUserPointer(GlfwWindow, this);
        glfwSetKeyCallback(GlfwWindow, DispatchKeyCallback);
        glfwSetMouseButtonCallback(GlfwWindow, DispatchMouseButtonCallback);
        glfwSetCursorPosCallback(GlfwWindow, DispatchCursorPosCallback);

        glfwFocusWindow(GlfwWindow);

        contextDescription.Window  = static_cast<void*>(GlfwWindow);
    }

    Window::~Window()
    {
        if (GlfwWindow)
        {
            glfwSetWindowUserPointer(GlfwWindow, nullptr);
            glfwDestroyWindow(GlfwWindow);
            GlfwWindow = nullptr;
        }
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

    bool Window::IsFocused() const
    {
        int newWidth;
        int newHeight;
        glfwGetFramebufferSize(GlfwWindow, &newWidth, &newHeight);

        if (Width != newWidth || Height != newHeight)
        {

        }

        Width = newWidth;
        Height = newHeight;
        return Width || Height;
    }

    CallbackSubscription Window::OnKey(KeyCallback callback)
    {
        if (!callback) return {};

        const CallbackSubscription callbackSubscription = MakeSubscriptionID();
        KeySubscriptions.push_back({callbackSubscription, std::move(callback)});
        return callbackSubscription;
    }

    CallbackSubscription Window::OnMouseButton(MouseButtonCallback callback)
    {
        if (!callback) return {};

        const CallbackSubscription callbackSubscription = MakeSubscriptionID();
        MouseButtonSubscriptions.push_back({callbackSubscription, std::move(callback)});
        return callbackSubscription;
    }

    CallbackSubscription Window::OnCursorMoved(CursorPosCallback callback)
    {
        if (!callback) return {};

        const CallbackSubscription callbackSubscription = MakeSubscriptionID();
        CursorPosSubscriptions.push_back({callbackSubscription, std::move(callback)});
        return callbackSubscription;
    }

    CallbackSubscription Window::OnResized(ResizeCallback callback)
    {
        if (!callback) return {};

        const CallbackSubscription callbackSubscription = MakeSubscriptionID();
        ResizeSubscriptions.push_back({callbackSubscription, std::move(callback)});
        return callbackSubscription;
    }

    void Window::UnsubscribeKey(const CallbackSubscription callbackSubscription)
    {
        std::erase_if(KeySubscriptions,[callbackSubscription](const KeySubscription& subscription)
        {
            return subscription.ID.Value == callbackSubscription.Value;
        });
    }

    void Window::UnsubscribeMouseButton(const CallbackSubscription callbackSubscription)
    {
        std::erase_if(MouseButtonSubscriptions, [callbackSubscription](const MouseButtonSubscription& subscription)
        {
            return subscription.ID.Value == callbackSubscription.Value;
        });
    }

    void Window::UnsubscribeCursorMoved(const CallbackSubscription callbackSubscription)
    {
        std::erase_if(CursorPosSubscriptions,[callbackSubscription](const CursorPosSubscription& subscription)
        {
            return subscription.ID.Value == callbackSubscription.Value;
        });
    }

    void Window::UnsubscribeResize(CallbackSubscription callbackSubscription)
    {
        std::erase_if(ResizeSubscriptions,[callbackSubscription](const ResizeSubscription& subscription)
        {
            return subscription.ID.Value == callbackSubscription.Value;
        });
    }

    CallbackSubscription Window::MakeSubscriptionID()
    {
        const CallbackSubscription callbackSubscription{.Value = NextSubscriptionID};
        ++NextSubscriptionID;
        return callbackSubscription;
    }

    Window* Window::FromGlfwWindow(GLFWwindow* window)
    {
        return static_cast<Window*>(glfwGetWindowUserPointer(window));
    }

    void Window::DispatchKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        Window* self = FromGlfwWindow(window);
        if (!self) return;

        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        for (const KeySubscription& subscription : self->KeySubscriptions)
        {
            if (subscription.Callback)
            {
                subscription.Callback(key, scancode, action, mods);
            }
        }
    }

    void Window::DispatchMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        const Window* self = FromGlfwWindow(window);
        if (!self) return;

        for (const MouseButtonSubscription& subscription : self->MouseButtonSubscriptions)
        {
            if (subscription.Callback)
            {
                subscription.Callback(button, action, mods);
            }
        }
    }

    void Window::DispatchCursorPosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        const Window* self = FromGlfwWindow(window);
        if (!self) return;

        for (const CursorPosSubscription& subscription : self->CursorPosSubscriptions)
        {
            if (subscription.Callback)
            {
                subscription.Callback(xpos, ypos);
            }
        }
    }
}

