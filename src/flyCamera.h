#pragma once

#include <glm/glm.hpp>

#include "renderGraphFile.h"
#include "window.h"
#include ".generated/eos/view.h"    // View

// A camera flown through the scene, and the flyCamera graph node that hands passes its View:
//
//     passes:
//       Camera:   { type: flyCamera, origin: [0, 1, 0], speed: 100 }
//       Geometry: { type: gbuffer }
//     edges:
//       - Camera.view -> Geometry.view
namespace EOS
{
    struct FlyCameraSettings final
    {
        glm::vec3 Origin{0.0f};
        glm::vec2 Rotation{0.0f};       // pitch and yaw, in degrees
        float Fov = 45.0f;              // vertical, in degrees
        float Speed = 45.0f;            // acceleration while a movement key is held, in units per second squared
        float Damping = 5.0f;           // how fast it slows down without one
        float NearPlane = 0.1f;
        float FarPlane = 25.0f;
    };

    /**
     * @brief Flown with WASD (Q up, E down) and turned while the right mouse button is held; space returns it to its
     *        origin. Input the UI takes (a focused text box, a click on a window) does not move it.
     */
    class FlyCamera final
    {
    public:
        FlyCamera(Window& window, const FlyCameraSettings& settings);
        ~FlyCamera();
        DELETE_COPY_MOVE(FlyCamera)

        // Settings take effect on the next Update; a new origin or rotation moves the camera there.
        void SetSettings(const FlyCameraSettings& settings);
        [[nodiscard]] const FlyCameraSettings& GetSettings() const { return Settings; }

        // Moves the camera by the input since the last update, and returns its view for an image of the given size.
        [[nodiscard]] View Update(glm::vec2 resolution);

        [[nodiscard]] glm::vec3 GetPosition() const { return Position; }
        [[nodiscard]] glm::vec3 GetForward() const;

    private:
        void SetMouseLook(bool enabled);

        Window& TargetWindow;
        FlyCameraSettings Settings;

        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        float Pitch = 0.0f;
        float Yaw = 0.0f;

        // Input.
        bool Forward = false;
        bool Backward = false;
        bool Left = false;
        bool Right = false;
        bool Up = false;
        bool Down = false;
        bool Reset = false;
        bool MouseLook = false;
        bool FirstMouseSample = true;
        glm::dvec2 LastMouse{0.0};

        double StartTime = 0.0;
        double LastUpdateTime = -1.0;
        uint32_t FrameIndex = 0;
        glm::mat4 PreviousViewProjection{1.0f};

        CallbackSubscription KeySubscription;
        CallbackSubscription MouseButtonSubscription;
        CallbackSubscription CursorSubscription;
    };

    /**
     * @brief Registers the flyCamera pass type: a node with a `view` output (eos.view's View, uploaded every frame) and
     *        the properties origin, rotation (pitch, yaw), fov, speed, damping, near and far. Each flyCamera pass of a
     *        graph file has a camera of its own, kept by pass name across reloads; a reload or UI edit that changes
     *        its origin or rotation moves it there.
     */
    void RegisterFlyCameraPass(PassRegistry& registry, Window& window);
}
