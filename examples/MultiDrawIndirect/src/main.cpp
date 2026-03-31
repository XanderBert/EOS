#include "../../Common/App.h"

struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;
    glm::vec4 cameraPos;
};

struct DrawData final
{
    uint32_t albedoID{};
    uint32_t normalID{};
    uint32_t metallicRoughnessID{};
    uint32_t pad{};
    glm::mat4 transform{};
};

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct FramePointers final
{
    uint64_t frameDataPtr;
    uint64_t drawDataPtr;
};

struct Resources final
{
    EOS::ShaderModuleHolder VertexShader;
    EOS::ShaderModuleHolder PixelShader;
    EOS::TextureHolder DepthTexture;
    EOS::BufferHolder VertexBuffer;
    EOS::BufferHolder IndexBuffer;
    EOS::BufferHolder PerDrawBuffer;
    EOS::BufferHolder PerFrameBuffer;
    EOS::BufferHolder IndirectDrawBuffer;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipeline;
};

Resources Handles;


int main()
{
    const EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - MultiDrawIndirect",
    };

    constexpr CameraDescription cameraDescription
    {
        .origin = {0.0f, 1.f, 0.0f},
        .rotation = {0, 0.0f},
        .acceleration = 100.0f
    };

    ExampleAppDescription appDescription
    {
        .contextDescription = contextDescr,
        .cameraDescription = cameraDescription
    };

    ExampleApp App{appDescription};

    Handles.VertexShader = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Vertex);
    Handles.PixelShader  = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Fragment);
    Handles.DepthTexture = App.CreateDepthTexture("Depth Buffer - MultiDrawIndirect");

    //TODO: This could be constevaled with reflection Or use Shader Resource Table model
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

    Scene scene = LoadModel("../data/sponza/Sponza.gltf", App.Context.get());
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


    std::vector<DrawData> drawData = BuildDrawDataFromScene<DrawData>(scene);

    Handles.PerDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "PerDrawBuffer",
    });

    Handles.PerFrameBuffer = App.Context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "PerFrameBuffer",
    });


    std::vector<EOS::DrawIndexedIndirectCommand> indirectCmds = BuildIndirectCommands(scene);

    Handles.IndirectDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectDrawBuffer",
    });


    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    const EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = Handles.VertexShader,
        .FragmentShader = Handles.PixelShader,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(Handles.DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipeline = App.Context->CreateRenderPipeline(renderPipelineDescription);


    const FramePointers framePointers
    {
        .frameDataPtr = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
    };


    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);
        if (std::isnan(aspectRatio)) return;

        glm::mat4 m = glm::mat4(1);
        const glm::mat4 mvp = App.MainCamera.GetViewProjectionMatrix(aspectRatio) * m;

        const PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .cameraPos = glm::vec4(App.MainCamera.GetPosition(), 0),
        };
        App.Context->Upload(Handles.PerFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);


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

        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        cmdPipelineBarrier(cmdBuffer, {},
            {
                { App.Context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { Handles.DepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite }
            });

        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Sponza", 0xff00f0ff);
            cmdBindVertexBuffer(cmdBuffer, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipeline);

            cmdPushConstants(cmdBuffer, framePointers);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectDrawBuffer, 0, scene.meshes.size());

            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    Handles = {};

    return 0;
}