#pragma once
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

    struct Window final
    {
        Window(ContextCreationDescription& contextDescription);
        ~Window();
        DELETE_COPY_MOVE(Window)

        void Poll();
        [[nodiscard] ]bool ShouldClose() const;
        [[nodiscard] ]bool IsFocused();

        GLFWwindow* GlfwWindow;
        int Width;
        int Height;
        
    private:
        static int glfwInstanceCount;
    };




}
