#pragma once
#include "glm/fwd.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

struct CameraDescription final
{
    glm::vec3 origin        = glm::vec3(0.0f);
    glm::vec2 rotation      = glm::vec2(0.0f);
    float fov               = 45.0f;
    float acceleration      = 45.0f;
    float damping           = 5.0f;
    float near              = 0.1f;
    float far               = 100.0f;
};

struct Camera final
{
    explicit Camera(const CameraDescription& cameraDescription)
    : Pitch(cameraDescription.rotation.x)
    , Yaw(cameraDescription.rotation.y)
    , Position(cameraDescription.origin)
    , Acceleration(cameraDescription.acceleration)
    , Damping(cameraDescription.damping)
    , Fov(cameraDescription.fov)
    , Near(cameraDescription.near)
    , Far(cameraDescription.far)
    {}

    [[nodiscard]] glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position, Position + GetForward(), Up);
    }

    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspectRatio) const
    {
        return glm::perspective(glm::radians(Fov), aspectRatio, Near, Far);
    }

    [[nodiscard]] glm::mat4 GetViewProjectionMatrix(float aspectRatio) const
    {
        return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
    }

    [[nodiscard]] glm::vec3 GetPosition() const
    {
        return Position;
    }

    void Update(const glm::vec3& direction, float deltaTime)
    {
        //Update Position
        const glm::vec3 forward = GetForward();
        const glm::vec3 right = GetRight();
        const glm::vec3 up = Up;
        const glm::mat3 basis(right, up, forward);

        const float dampingFactor = std::exp(-Damping * deltaTime);
        Velocity *= dampingFactor;

        glm::vec3 desiredDir = basis * direction;
        if (glm::length(desiredDir) > 0.0f)
        {
            desiredDir = glm::normalize(desiredDir);
            Velocity += desiredDir * deltaTime * Acceleration;
        }
        Position += Velocity * deltaTime;
    }

    float Pitch{};
    float Yaw{};

private:
    [[nodiscard]] glm::vec3 GetForward() const
    {
        glm::vec3 forward;
        forward.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        forward.y = sin(glm::radians(Pitch));
        forward.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        return glm::normalize(forward);
    }

    [[nodiscard]] glm::vec3 GetRight() const
    {
        return glm::normalize(glm::cross(GetForward(), Up));
    }

    glm::vec3 Position;
    glm::vec3 Up {0.0f, 1.0f, 0.0f};
    glm::vec3 Velocity{};

    float Acceleration  = 45.0f;
    float Damping  = 5.0f;

    float Fov;
    float Near;
    float Far;
};