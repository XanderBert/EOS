#include "../../Common/App.h"
#include ".generated/modelAlbedo.h"

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Resources final
{
    EOS::ShaderProgramHolder Shader;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipeline;
    EOS::Holder<EOS::BufferHandle> VertexBuffer;
    EOS::Holder<EOS::BufferHandle> IndexBuffer;
    EOS::Holder<EOS::BufferHandle> PerFrameBuffer;
};

Resources Handles;

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Model PBR",
    };

    CameraDescription cameraDescription
    {
        .origin = {0.0f, 0.0f, -3.5f},
        .rotation = {0, 90.0f}
    };

    ExampleAppDescription appDescription
    {
        .contextDescription = contextDescr,
        .cameraDescription = cameraDescription
    };

    ExampleApp App{appDescription};


    Handles.Shader = App.Context->CreateShaderProgram({.Module = "modelAlbedo"});

    //TODO: This could be constevaled with reflection
    constexpr EOS::VertexInputData vdesc
    {
        .Attributes =
    {
            { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) },
            { .Location = 1, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, normal) },
            { .Location = 2, .Format = EOS::VertexFormat::Float2, .Offset = offsetof(Vertex, uv) },
            { .Location = 3, .Format = EOS::VertexFormat::Float4, .Offset = offsetof(Vertex, tangent) }
        },

        .InputBindings =
        {
            { .Stride = sizeof(Vertex) }
        }
    };


    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = {Handles.Shader, "vertexMain"},
        .FragmentShader = {Handles.Shader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32,
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipeline = App.Context->CreateRenderPipeline(renderPipelineDescription);


    Scene scene = LoadModel("../data/damaged_helmet/DamagedHelmet.gltf", App.Context.get());
    std::vector<Vertex> vertices = BuildVerticesFromScene<Vertex>(scene);

    Handles.VertexBuffer = App.Context->CreateBuffer(
    {
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });

    Handles.IndexBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::Index,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(uint32_t) * scene.indices.size(),
        .Data      = scene.indices.data(),
        .DebugName = "Buffer: index"
    });

    Handles.PerFrameBuffer = App.Context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "PerFrameBuffer",
    });


    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);

        glm::mat4 m = glm::rotate(glm::mat4(1.0f),glm::radians(90.0f) , glm::vec3(1.0f, 0.0f, 0.0f));
        m = rotate(m, static_cast<float>(glfwGetTime()), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::mat4 mvp = App.MainCamera.GetViewProjectionMatrix(aspectRatio) * m;

        PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .cameraPos = App.MainCamera.GetPosition(),
            .material = scene.meshes[0].material,
        };
        perFrameData.material.samplerState = App.DefaultSampler;


        constexpr EOS::DepthState depthState
        {
            .CompareOpState = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };

        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
        const EOS::GraphTexture depth = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
        const EOS::GraphBuffer perFrame = graph.ImportBuffer(Handles.PerFrameBuffer, "PerFrameBuffer");

        graph.AddUpload("Upload Frame Data", perFrame, perFrameData);

        graph.AddRasterPass("Damaged Helmet")
            .Color(EOS::Clear(backbuffer, {0.36f, 0.4f, 1.0f, 0.28f}))
            .Depth(EOS::ClearDepth(depth))
            .Read(perFrame)
            .Execute([&](EOS::PassContext& pass)
        {
            cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipeline);
            cmdPushConstants(pass.Cmd, FramePointers{.perFrame = pass.Address(perFrame)});
            cmdSetDepthState(pass.Cmd, depthState);
            cmdDrawIndexed(pass.Cmd, scene.indices.size());
        });

        graph.Execute();
    });

    Handles = {};

    return 0;
}