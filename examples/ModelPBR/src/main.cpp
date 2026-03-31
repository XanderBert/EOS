#include "../../Common/App.h"

struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;

    glm::vec3 cameraPos;
    uint32_t  albedoID;

    uint32_t normalID;
    uint32_t metallicRoughnessID;
    uint32_t pad01;
    uint32_t pad02;
};

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Resources final
{
    EOS::Holder<EOS::ShaderModuleHandle> ShaderHandleVert;
    EOS::Holder<EOS::ShaderModuleHandle> ShaderHandleFrag;
    EOS::Holder<EOS::TextureHandle> DepthTexture;
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


    Handles.ShaderHandleVert = App.Context->CreateShaderModule("modelAlbedo", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleFrag = App.Context->CreateShaderModule("modelAlbedo", EOS::ShaderStage::Fragment);

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

    Handles.DepthTexture = App.CreateDepthTexture("Depth Buffer - ModelPBR");

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = Handles.ShaderHandleVert,
        .FragmentShader = Handles.ShaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(Handles.DepthTexture),
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

        const PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .cameraPos = App.MainCamera.GetPosition(),
            .albedoID = scene.meshes[0].albedoTextureIdx,
            .normalID = scene.meshes[0].normalTextureIdx,
            .metallicRoughnessID = scene.meshes[0].metallicRoughnessTextureIdx,
        };


        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        EOS::Framebuffer framebuffer
        {
            .Color = {{.Texture = App.Context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = Handles.DepthTexture },
            .DebugName = "Basic Color Depth Framebuffer"
        };

        constexpr EOS::RenderPass renderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
            .Depth{ .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
        };

        constexpr EOS::DepthState depthState
        {
            .CompareOpState = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };


        cmdPipelineBarrier(cmdBuffer, {},
            {
                { App.Context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { Handles.DepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite }
            });

        cmdUpdateBuffer(cmdBuffer, Handles.PerFrameBuffer, perFrameData);
        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Damaged Helmet", 0xff0000ff);
            cmdBindVertexBuffer(cmdBuffer, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipeline);

            struct FramePointers
            {
                uint64_t draw;
            }pc
            {
                .draw = App.Context->GetGPUAddress(Handles.PerFrameBuffer)
            };

            cmdPushConstants(cmdBuffer, pc);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexed(cmdBuffer, scene.indices.size());
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    Handles = {};

    return 0;
}