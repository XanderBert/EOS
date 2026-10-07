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
        using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
        using CharCallback = std::function<void(uint32_t codepoint)>;

        Window(ContextCreationDescription& contextDescription);
        ~Window();
        DELETE_COPY_MOVE(Window)

        /**
         * @brief Processes window events, and paces the render loop by what can be seen of the window: while it is
         *        minimized this waits until it is restored (or closed); while it is in the background (not focused)
         *        it returns at a low rate, BackgroundFrameInterval. Any event, such as a click, focus or a resize, ends
         *        the wait early.
         * @note  An unfocused window keeps rendering, slowly: it can still be in view, and on Wayland a window only
         *        appears once it presented a frame, so a loop that waited for focus would never show it.
         */
        void Poll();
        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] bool IsFocused() const;
        // Minimized, or without a framebuffer to render to.
        [[nodiscard]] bool IsMinimized() const;

        static constexpr double BackgroundFrameInterval = 0.1;     // seconds between frames while not focused

        CallbackSubscription OnKey(KeyCallback callback);
        CallbackSubscription OnMouseButton(MouseButtonCallback callback);
        CallbackSubscription OnCursorMoved(CursorPosCallback callback);
        CallbackSubscription OnResized(ResizeCallback callback);
        CallbackSubscription OnScroll(ScrollCallback callback);
        CallbackSubscription OnChar(CharCallback callback);

        void UnsubscribeKey(CallbackSubscription callbackSubscription);
        void UnsubscribeMouseButton(CallbackSubscription callbackSubscription);
        void UnsubscribeCursorMoved(CallbackSubscription callbackSubscription);
        void UnsubscribeResize(CallbackSubscription callbackSubscription);
        void UnsubscribeScroll(CallbackSubscription callbackSubscription);
        void UnsubscribeChar(CallbackSubscription callbackSubscription);

        GLFWwindow* GlfwWindow = nullptr;
        int Width{};
        int Height{};
    private:
        // Reads the framebuffer size and tells the resize subscribers when it changed.
        void UpdateFramebufferSize();

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

        struct ScrollSubscription final
        {
            CallbackSubscription ID;
            ScrollCallback Callback;
        };

        struct CharSubscription final
        {
            CallbackSubscription ID;
            CharCallback Callback;
        };

        std::vector<KeySubscription> KeySubscriptions;
        std::vector<MouseButtonSubscription> MouseButtonSubscriptions;
        std::vector<CursorPosSubscription> CursorPosSubscriptions;
        std::vector<ResizeSubscription> ResizeSubscriptions;
        std::vector<ScrollSubscription> ScrollSubscriptions;
        std::vector<CharSubscription> CharSubscriptions;

        uint64_t NextSubscriptionID = 1;

        [[nodiscard]] CallbackSubscription MakeSubscriptionID();

        static Window* FromGlfwWindow(GLFWwindow* window);
        static void DispatchKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void DispatchMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void DispatchCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void DispatchScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
        static void DispatchCharCallback(GLFWwindow* window, unsigned int codepoint);
    };
}
