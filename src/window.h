#pragma once
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "defines.h"


namespace EOS
{
    //Forward declare
    struct ContextCreationDescription;

    struct Window final
    {
        DELETE_COPY_MOVE(Window)

        //Fills in the window data in the Context Description
        static GLFWwindow* InitWindow(ContextCreationDescription& contextDescription);

        inline static void DestroyWindow(GLFWwindow* window)
        {
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    };




}
