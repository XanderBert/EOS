#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// https://searchfox.org/mozilla-central/source/gfx/src/X11UndefineNone.h
// The header <X11/X.h> defines "None" as a macro that expands to "0L".
// This is terrible because many enumerations have an enumerator named "None".
// To work around this, we undefine the macro "None", and define a replacement
// macro named "X11None".
// Include this header after including X11 headers, where necessary.
#ifdef None
#  undef None
#  define X11None 0L
// <X11/X.h> also defines "RevertToNone" as a macro that expands to "(int)None".
// Since we are undefining "None", that stops working. To keep it working,
// we undefine "RevertToNone" and redefine it in terms of "X11None".
#  ifdef RevertToNone
#    undef RevertToNone
#    define RevertToNone (int)X11None
#  endif
#endif

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
        inline static int Width;
        inline static int Height;

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
