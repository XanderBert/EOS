#include "flyCamera.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "UI/UI.h"

namespace EOS
{
    namespace
    {
        constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
    }

    FlyCamera::FlyCamera(Window& window, const FlyCameraSettings& settings)
    : TargetWindow(window)
    {
        SetSettings(settings);
        StartTime = glfwGetTime();

        KeySubscription = TargetWindow.OnKey([this](int key, int, int action, int)
        {
            const bool pressed = action != GLFW_RELEASE;

            // Releases always go through, so a key held before the UI took the keyboard does not stick.
            if (pressed && UI::WantCaptureKeyboard()) return;

            switch (key)
            {
                case GLFW_KEY_W:     Forward = pressed; break;
                case GLFW_KEY_S:     Backward = pressed; break;
                case GLFW_KEY_A:     Left = pressed; break;
                case GLFW_KEY_D:     Right = pressed; break;
                case GLFW_KEY_Q:     Up = pressed; break;
                case GLFW_KEY_E:     Down = pressed; break;
                case GLFW_KEY_SPACE: Reset = pressed; break;
                default: break;
            }
        });

        MouseButtonSubscription = TargetWindow.OnMouseButton([this](int button, int action, int)
        {
            if (button != GLFW_MOUSE_BUTTON_RIGHT) return;

            if (action == GLFW_PRESS && !UI::WantCaptureMouse()) SetMouseLook(true);
            else if (action == GLFW_RELEASE && MouseLook) SetMouseLook(false);
        });

        CursorSubscription = TargetWindow.OnCursorMoved([this](double x, double y)
        {
            if (!MouseLook) return;

            const glm::dvec2 mouse{x, y};
            if (FirstMouseSample)
            {
                LastMouse = mouse;
                FirstMouseSample = false;
                return;
            }

            constexpr float sensitivity = 0.1f;
            const glm::vec2 offset = glm::vec2(mouse - LastMouse) * sensitivity;
            LastMouse = mouse;

            Yaw += offset.x;
            Pitch = glm::clamp(Pitch - offset.y, -89.0f, 89.0f);   // window y goes down
        });
    }

    FlyCamera::~FlyCamera()
    {
        if (MouseLook) SetMouseLook(false);
        TargetWindow.UnsubscribeKey(KeySubscription);
        TargetWindow.UnsubscribeMouseButton(MouseButtonSubscription);
        TargetWindow.UnsubscribeCursorMoved(CursorSubscription);
    }

    void FlyCamera::SetSettings(const FlyCameraSettings& settings)
    {
        if (settings.Origin != Settings.Origin || settings.Rotation != Settings.Rotation || LastUpdateTime < 0.0)
        {
            Position = settings.Origin;
            Pitch = settings.Rotation.x;
            Yaw = settings.Rotation.y;
            Velocity = glm::vec3(0.0f);
        }
        Settings = settings;
    }

    glm::vec3 FlyCamera::GetForward() const
    {
        const float pitch = glm::radians(Pitch);
        const float yaw = glm::radians(Yaw);
        return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
    }

    View FlyCamera::Update(glm::vec2 resolution)
    {
        // Mouse look holds on to the cursor; give it back when the user switches away.
        if (MouseLook && !TargetWindow.IsFocused()) SetMouseLook(false);

        const double now = glfwGetTime();
        const float deltaTime = LastUpdateTime < 0.0 ? 0.0f : static_cast<float>(now - LastUpdateTime);
        LastUpdateTime = now;

        if (Reset)
        {
            Position = Settings.Origin;
            Pitch = Settings.Rotation.x;
            Yaw = Settings.Rotation.y;
            Velocity = glm::vec3(0.0f);
        }

        const glm::vec3 forward = GetForward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, kUp));
        const glm::vec3 input{static_cast<float>(Right) - static_cast<float>(Left), static_cast<float>(Up) - static_cast<float>(Down), static_cast<float>(Forward) - static_cast<float>(Backward)};

        Velocity *= std::exp(-Settings.Damping * deltaTime);
        const glm::vec3 direction = glm::mat3(right, kUp, forward) * input;
        if (glm::length(direction) > 0.0f) Velocity += glm::normalize(direction) * deltaTime * Settings.Speed;
        Position += Velocity * deltaTime;

        const float aspectRatio = resolution.y > 0.0f ? resolution.x / resolution.y : 1.0f;
        const glm::mat4 view = glm::lookAt(Position, Position + forward, kUp);
        const glm::mat4 projection = glm::perspective(glm::radians(Settings.Fov), aspectRatio, Settings.NearPlane, Settings.FarPlane);
        const glm::mat4 viewProjection = projection * view;

        const View result
        {
            .view = view,
            .projection = projection,
            .viewProjection = viewProjection,
            .inverseViewProjection = glm::inverse(viewProjection),
            .previousViewProjection = FrameIndex == 0 ? viewProjection : PreviousViewProjection,
            .position = Position,
            .nearPlane = Settings.NearPlane,
            .forward = forward,
            .farPlane = Settings.FarPlane,
            .resolution = resolution,
            .time = static_cast<float>(now - StartTime),
            .deltaTime = deltaTime,
            .frameIndex = FrameIndex,
        };

        PreviousViewProjection = viewProjection;
        ++FrameIndex;
        return result;
    }

    void FlyCamera::SetMouseLook(bool enabled)
    {
        MouseLook = enabled;
        FirstMouseSample = true;

        // While the camera owns the cursor, the UI should not react to it. Keyboard navigation makes a focused UI
        // window capture WASD, and the right click that starts mouse look does not unfocus it, so drop focus too.
        UI::SetMouseInputEnabled(!enabled);
        if (enabled) UI::ClearFocus();

        glfwSetInputMode(TargetWindow.GlfwWindow, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(TargetWindow.GlfwWindow, GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
    }

    void RegisterFlyCamera(PassRegistry& registry, Window& window)
    {
        // A camera per pass name, so a reload keeps where it is. One no pass used in the last updates of the others is
        // released: its pass was renamed or removed. Counted in updates, not time, so a minimized window keeps them.
        struct Entry final
        {
            std::unique_ptr<FlyCamera> Camera;
            uint64_t LastUsed = 0;
        };
        struct Cameras final
        {
            std::map<std::string, Entry, std::less<>> Entries;
            uint64_t Updates = 0;
        };
        auto cameras = std::make_shared<Cameras>();

        constexpr FlyCameraSettings defaults{};
        registry.Register(
        {
            .Name = "flyCamera",
            .Pins = {EOS::Pin::Typed(EOS::Pin::BufferOutput("view", 0), "View")},
            .Properties =
            {
                EOS::Property::Float3("origin", defaults.Origin),
                EOS::Property::Float2("rotation", defaults.Rotation),
                EOS::Property::Float("fov", defaults.Fov, 10.0f, 120.0f),
                EOS::Property::Float("speed", defaults.Speed, 1.0f, 200.0f),
                EOS::Property::Float("damping", defaults.Damping, 0.0f, 20.0f),
                EOS::Property::Float("near", defaults.NearPlane),
                EOS::Property::Float("far", defaults.FarPlane),
            },
            .Setup = [cameras, &window](PassSetup& setup)
            {
                const PassData& data = setup.Data;
                const FlyCameraSettings settings
                {
                    .Origin = data.Float3("origin"),
                    .Rotation = data.Float2("rotation"),
                    .Fov = data.Float("fov"),
                    .Speed = data.Float("speed"),
                    .Damping = data.Float("damping"),
                    .NearPlane = data.Float("near"),
                    .FarPlane = data.Float("far"),
                };

                const uint64_t now = ++cameras->Updates;
                std::erase_if(cameras->Entries, [now](const auto& entry) { return now - entry.second.LastUsed > 120; });

                Entry& entry = cameras->Entries[data.Name()];
                entry.LastUsed = now;
                if (!entry.Camera) entry.Camera = std::make_unique<FlyCamera>(window, settings);
                else entry.Camera->SetSettings(settings);
                FlyCamera* camera = entry.Camera.get();

                const Dimensions size = setup.Graph.GetSwapchainSize();
                const View view = camera->Update(glm::vec2(static_cast<float>(size.Width), static_cast<float>(size.Height)));

                setup.Upload("view", view);
            },
        });
    }
}
