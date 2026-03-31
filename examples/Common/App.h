#pragma once
#include "ModelLoader.h"
#include "ExampleHelpers.h"
#include "Camera.h"
#include "EOS.h"
#include "imgui.h"
#include "ImGuiRenderer.h"
#include "glm/gtc/type_ptr.hpp"

struct InputState final
{
    bool forward{};
    bool backward{};
    bool left{};
    bool right{};
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
            .mipLodMax = EOS_MAX_MIP_LEVELS,
            .maxAnisotropic = 0,
            .debugName = "Linear Sampler",
        };
        DefaultSampler = Context->CreateSampler(samplerDescription);
        SetupInputCallbacks();


        ImGuiRenderer = std::make_unique<EOS::ImGuiRenderer>(Context.get(), Window);
    }

    DELETE_COPY_MOVE(ExampleApp)

    [[nodiscard]] EOS::Holder<EOS::TextureHandle> CreateDepthTexture(const char* debugName = "Depth Buffer") const
    {
        return Context->CreateTexture(
        {
            .Type                   = EOS::ImageType::Image_2D,
            .TextureFormat          = EOS::Format::Z_F32,
            .TextureDimensions      = {static_cast<uint32_t>(Window.Width), static_cast<uint32_t>(Window.Height)},
            .Usage                  = EOS::TextureUsageFlags::Attachment,
            .DebugName              = debugName,
        });
    }

    Camera MainCamera;
    EOS::Window Window;
    std::unique_ptr<EOS::IContext> Context;
    std::unique_ptr<EOS::ImGuiRenderer> ImGuiRenderer;
    EOS::Holder<EOS::SamplerHandle> DefaultSampler;
    InputState Input;
    float DeltaTime{};

    template <typename Function>
    void Run(Function&& renderLoop)
    {
        lastTime = glfwGetTime();

        while (!Window.ShouldClose() && !ShouldExit)
        {
            Window.Poll();
            if (!Window.IsFocused())
            {
                if (Input.rightMouse) SetMouseLookMode(false);
                continue;
            }

            //Update time
            const float currentTime = glfwGetTime();
            DeltaTime = currentTime - lastTime;
            lastTime = currentTime;

            //Update Camera
            if (Input.space)
            {
                MainCamera.SetPosition(StartingCameraPosition);
                MainCamera.SetRotation(StartingCameraRotation);
            }
            glm::vec3 direction{Input.right - Input.left, 0.0f, Input.forward - Input.backward};
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

        ImGuiIO& io = ImGui::GetIO();
        if (enabled)
        {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
        else
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

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
            Input.forward = key == GLFW_KEY_W && pressed;
            Input.backward = key == GLFW_KEY_S && pressed;
            Input.left = key == GLFW_KEY_A && pressed;
            Input.right = key == GLFW_KEY_D && pressed;
            Input.space = key == GLFW_KEY_SPACE && pressed;

            if (key == GLFW_KEY_MINUS)
            {
                Context->ReloadShaders();
            }
        });

        Window.OnMouseButton([this](int button, int action, int)
        {
            if (button != GLFW_MOUSE_BUTTON_LEFT) return;

            if (action == GLFW_PRESS)
            {
                if (ImGui::GetIO().WantCaptureMouse) return;
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

    float lastTime{};
    bool FirstMouseSample = true;
    double LastMouseX = 0.0;
    double LastMouseY = 0.0;

    glm::vec3 StartingCameraPosition;
    glm::vec2 StartingCameraRotation;

    bool ShouldExit = false;
};