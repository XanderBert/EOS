#include "../../Common/App.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

// The frame is graphs/shadowMapping.yaml. The shadow map is rendered from a light at a position, looking along the
// sun: the lightView data below, written in C++, builds that light's View from the sun's direction.
int main()
{
    ExampleApp App{{.Name = "EOS - ShadowMapping"}};

    App.Passes.Register(
    {
        .Name = "lightView",
        .Pins =
        {
            EOS::Pin::Typed(EOS::Pin::ReadBuffer("light"), "DirectionalLight"),
            EOS::Pin::Typed(EOS::Pin::BufferOutput("view", 0), "View"),
        },
        .Properties =
        {
            EOS::Property::Float3("position", glm::vec3(0.0f, 20.0f, 20.0f)),
            EOS::Property::Float("extent", 25.0f, 1.0f, 100.0f),        // half the width and height it covers
            EOS::Property::Float("near", 0.1f),
            EOS::Property::Float("far", 50.0f),
        },
        .Setup = [](EOS::PassSetup& setup)
        {
            const EOS::PassData& data = setup.Data;
            const EOS::DirectionalLight* sun = data.Host<EOS::DirectionalLight>("light");
            if (!sun) return;

            const glm::vec3 position = data.Float3("position");
            const float extent = data.Float("extent");
            const glm::mat4 view = glm::lookAt(position, position + sun->direction, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 projection = glm::ortho(-extent, extent, -extent, extent, data.Float("near"), data.Float("far"));
            const glm::mat4 viewProjection = projection * view;

            setup.Upload("view", EOS::View
            {
                .view = view,
                .projection = projection,
                .viewProjection = viewProjection,
                .inverseViewProjection = glm::inverse(viewProjection),
                .previousViewProjection = viewProjection,
                .position = position,
                .nearPlane = data.Float("near"),
                .forward = sun->direction,
                .farPlane = data.Float("far"),
            });
        },
    });

    App.Run("shadowMapping");
    return 0;
}
