#include "sun.h"

#include <cmath>

namespace EOS
{
    glm::vec3 DirectionFromRotation(glm::vec2 rotation)
    {
        const float pitch = glm::radians(rotation.x);
        const float yaw = glm::radians(rotation.y);
        return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
    }

    void RegisterSun(PassRegistry& registry)
    {
        registry.Register(
        {
            .Name = "sun",
            .Pins = {EOS::Pin::Typed(EOS::Pin::BufferOutput("light", 0), "DirectionalLight")},
            .Properties =
            {
                EOS::Property::Float2("rotation", glm::vec2(-90.0f, 0.0f), -180.0f, 180.0f),
                EOS::Property::Float3("color", glm::vec3(1.0f), 0.0f, 1.0f),
                EOS::Property::Float("intensity", 1.0f, 0.0f, 10.0f),
            },
            .Setup = [](PassSetup& setup)
            {
                const PassData& data = setup.Data;
                setup.Upload("light", DirectionalLight
                {
                    .direction = DirectionFromRotation(data.Float2("rotation")),
                    .radiance = data.Float3("color") * data.Float("intensity"),
                });
            },
        });
    }
}
