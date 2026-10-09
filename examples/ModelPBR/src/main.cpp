#include "../../Common/App.h"
#include ".generated/model.h"
#include "glm/ext/matrix_transform.hpp"

// The frame is graphs/modelPBR.yaml. What a graph file cannot do is written in C++, like the turntable below: data the
// file lists under 'data', the model's transform uploaded every frame (ObjectTransform, declared in shaders/model.slang).
int main()
{
    ExampleApp App{{.Name = "EOS - Model PBR"}};

    App.Passes.Register(
    {
        .Name = "turntable",
        .Pins = {EOS::Pin::Typed(EOS::Pin::BufferOutput("transform", 0), "ObjectTransform")},
        .Properties = {EOS::Property::Float("speed", 1.0f, -3.0f, 3.0f)},     // radians per second, about the up axis
        .Setup = [angle = 0.0f, lastTime = glfwGetTime()](EOS::PassSetup& setup) mutable
        {
            const double now = glfwGetTime();
            angle += setup.Data.Float("speed") * static_cast<float>(now - lastTime);
            lastTime = now;
            setup.Upload("transform", ObjectTransform{.model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f))});
        },
    });

    App.Run("modelPBR");
    return 0;
}
