#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include <GLFW/glfw3.h>

#include "defines.h"
namespace EOS
{
    //Forward declare
    struct ContextCreationDescription;

    struct CallbackSubscription final
    {
        uint64_t Value = 0;

        [[nodiscard]] bool Valid() const
        {
            return Value != 0;
        }
    };

    struct Window final
    {
        using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
        using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
        using CursorPosCallback = std::function<void(double xpos, double ypos)>;
        using ResizeCallback = std::function<void(int width, int height)>;

        Window(ContextCreationDescription& contextDescription);
        ~Window();
        DELETE_COPY_MOVE(Window)

        void Poll();
        [[nodiscard] ]bool ShouldClose() const;
        [[nodiscard] ]bool IsFocused() const;

        CallbackSubscription OnKey(KeyCallback callback);
        CallbackSubscription OnMouseButton(MouseButtonCallback callback);
        CallbackSubscription OnCursorMoved(CursorPosCallback callback);
        CallbackSubscription OnResized(ResizeCallback callback);

        void UnsubscribeKey(CallbackSubscription callbackSubscription);
        void UnsubscribeMouseButton(CallbackSubscription callbackSubscription);
        void UnsubscribeCursorMoved(CallbackSubscription callbackSubscription);
        void UnsubscribeResize(CallbackSubscription callbackSubscription);

        GLFWwindow* GlfwWindow = nullptr;
        int Width{};
        int Height{};
    private:

        struct KeySubscription final
        {
            CallbackSubscription ID;
            KeyCallback Callback;
        };

        struct MouseButtonSubscription final
        {
            CallbackSubscription ID;
            MouseButtonCallback Callback;
        };

        struct CursorPosSubscription final
        {
            CallbackSubscription ID;
            CursorPosCallback Callback;
        };

        struct ResizeSubscription final
        {
            CallbackSubscription ID;
            ResizeCallback Callback;
        };

        std::vector<KeySubscription> KeySubscriptions;
        std::vector<MouseButtonSubscription> MouseButtonSubscriptions;
        std::vector<CursorPosSubscription> CursorPosSubscriptions;
        std::vector<ResizeSubscription> ResizeSubscriptions;

        uint64_t NextSubscriptionID = 1;

        [[nodiscard]] CallbackSubscription MakeSubscriptionID();

        static Window* FromGlfwWindow(GLFWwindow* window);
        static void DispatchKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void DispatchMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void DispatchCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    };
}
