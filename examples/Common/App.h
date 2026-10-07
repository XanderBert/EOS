#pragma once
#include "ModelLoader.h"
#include "ExampleHelpers.h"
#include "Camera.h"
#include "EOS.h"
#include "renderGraph.h"
#include "UI/UI.h"
#include "glm/gtc/type_ptr.hpp"

struct InputState final
{
    bool forward{};
    bool backward{};
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool rightMouse{};
    bool space{};
};

struct ExampleAppDescription final
{
    EOS::ContextCreationDescription contextDescription;
    CameraDescription cameraDescription;
};

class ExampleApp final
{
public:
    explicit ExampleApp(ExampleAppDescription& appDescription)
    :   MainCamera(appDescription.cameraDescription)
    ,   Window(appDescription.contextDescription)
    ,   Context(EOS::CreateContextWithSwapChain(appDescription.contextDescription))
    ,   StartingCameraPosition(appDescription.cameraDescription.origin)
    ,   StartingCameraRotation(appDescription.cameraDescription.rotation)
    {
        //Create Default Sampler
        constexpr EOS::SamplerDescription samplerDescription
        {
            .mipMap = EOS::SamplerMip::Linear,
            .mipLodMax = EOS_MAX_MIP_LEVELS,
            .maxAnisotropic = 0,
            .debugName = "Linear Sampler",
        };
        DefaultSampler = Context->CreateSampler(samplerDescription);
        SetupInputCallbacks();

        Graph = std::make_unique<EOS::RenderGraph>(Context.get());


        UIRenderer = std::make_unique<EOS::UI::Renderer>(Context.get(), Window);
    }

    DELETE_COPY_MOVE(ExampleApp)

    // Declares the UI of this frame (declareWidgets calls EOS::UI functions) and draws it on top of target. Textures the
    // UI shows have to be imported into the graph and added with .Sample() on the returned pass.
    template <typename Function>
    EOS::PassBuilder AddUIPass(EOS::GraphTexture target, Function&& declareWidgets)
    {
        UIRenderer->NewFrame();
        std::forward<Function>(declareWidgets)();

        EOS::PassBuilder pass = Graph->AddRasterPass("UI").Color(EOS::Load(target));
        pass.Execute([this](EOS::PassContext& context)
        {
            UIRenderer->Render(context.Cmd);
        });
        return pass;
    }

    Camera MainCamera;
    EOS::Window Window;
    std::unique_ptr<EOS::IContext> Context;
    std::unique_ptr<EOS::RenderGraph> Graph;        // destroyed before the context, which its textures belong to
    std::unique_ptr<EOS::UI::Renderer> UIRenderer;
    EOS::Holder<EOS::SamplerHandle> DefaultSampler;
    InputState Input;
    float DeltaTime{};

    // Return true when the cursor (GLFW window coordinates) is over UI, so a click there goes to
    // the UI instead of starting mouse-look. Defaults to the UI's own answer, which is false in a build
    // without a UI.
    std::function<bool(double xpos, double ypos)> WantCaptureMouse = [](double, double)
    {
        return EOS::UI::WantCaptureMouse();
    };

    // Return true while UI wants keyboard input (e.g. a focused text box), so key presses don't
    // drive the camera. Defaults to the UI's own answer.
    std::function<bool()> WantCaptureKeyboard = []()
    {
        return EOS::UI::WantCaptureKeyboard();
    };

    template <typename Function>
    void Run(Function&& renderLoop)
    {
        lastTime = glfwGetTime();

        while (!Window.ShouldClose() && !ShouldExit)
        {
            Window.Poll();

            // Mouse look holds on to the cursor; give it back when the user switches away.
            if (!Window.IsFocused() && Input.rightMouse) SetMouseLookMode(false);

            //Update time
            const double currentTime = glfwGetTime();
            DeltaTime = static_cast<float>(currentTime - lastTime);
            lastTime = currentTime;

            //Update Camera
            if (Input.space)
            {
                MainCamera.SetPosition(StartingCameraPosition);
                MainCamera.SetRotation(StartingCameraRotation);
            }
            glm::vec3 direction{Input.right - Input.left, Input.up - Input.down, Input.forward - Input.backward};
            MainCamera.Update(direction, DeltaTime);


            //Render
            std::forward<Function>(renderLoop)();
        }
    }

    void Exit()
    {
        ShouldExit = true;
    }
private:

    void SetMouseLookMode(bool enabled)
    {
        Input.rightMouse = enabled;
        FirstMouseSample = true;

        // While the application owns the cursor for mouse look, the UI should not react to it.
        EOS::UI::SetMouseInputEnabled(!enabled);

        // Keyboard navigation makes a focused UI window capture WASD, and unlike a left click
        // the right click that starts mouse look doesn't unfocus it, so drop focus explicitly.
        if (enabled) EOS::UI::ClearFocus();

        glfwSetInputMode(Window.GlfwWindow, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
        {
            glfwSetInputMode(Window.GlfwWindow, GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    void SetupInputCallbacks()
    {
        Window.OnKey([this](int key, int, int action, int)
        {
            const bool pressed = action != GLFW_RELEASE;

            // Releases always go through so a key held before UI took focus doesn't get stuck.
            if (pressed && WantCaptureKeyboard && WantCaptureKeyboard()) return;

            switch (key)
            {
                case GLFW_KEY_W:     Input.forward = pressed; break;
                case GLFW_KEY_S:     Input.backward = pressed; break;
                case GLFW_KEY_A:     Input.left = pressed; break;
                case GLFW_KEY_D:     Input.right = pressed; break;
                case GLFW_KEY_Q:     Input.up = pressed; break;
                case GLFW_KEY_E:     Input.down = pressed; break;
                case GLFW_KEY_SPACE: Input.space = pressed; break;
                default: break;
            }

            if (key == GLFW_KEY_MINUS)
            {
                Context->ReloadShaders();
            }
        });

        Window.OnMouseButton([this](int button, int action, int)
        {
            if (button != GLFW_MOUSE_BUTTON_RIGHT) return;

            if (action == GLFW_PRESS)
            {
                if (WantCaptureMouse)
                {
                    double xpos, ypos;
                    glfwGetCursorPos(Window.GlfwWindow, &xpos, &ypos);
                    if (WantCaptureMouse(xpos, ypos)) return;
                }
                SetMouseLookMode(true);
            }
            else if (action == GLFW_RELEASE && Input.rightMouse)
            {
                SetMouseLookMode(false);
            }
        });

        Window.OnCursorMoved([this](double xpos, double ypos)
        {
            if (!Input.rightMouse) return;

            if (FirstMouseSample)
            {
                LastMouseX = xpos;
                LastMouseY = ypos;
                FirstMouseSample = false;
                return;
            }

            float xoffset = static_cast<float>(xpos - LastMouseX);
            float yoffset = static_cast<float>(LastMouseY - ypos); // reversed since y-coords go down
            LastMouseX = xpos;
            LastMouseY = ypos;

            constexpr float mouseSensitivity = 0.1f;
            xoffset *= mouseSensitivity;
            yoffset *= mouseSensitivity;

            MainCamera.Yaw   += xoffset;
            MainCamera.Pitch += yoffset;

            // clamp pitch so we don't flip upside down
            if (MainCamera.Pitch > 89.0f)  MainCamera.Pitch = 89.0f;
            if (MainCamera.Pitch < -89.0f) MainCamera.Pitch = -89.0f;
        });
    }

    double lastTime{};
    bool FirstMouseSample = true;
    double LastMouseX = 0.0;
    double LastMouseY = 0.0;

    glm::vec3 StartingCameraPosition;
    glm::vec2 StartingCameraRotation;

    bool ShouldExit = false;
};