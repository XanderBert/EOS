#include "../../common.h"
#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"
#include "utils.h"

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

    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(App.Context, App.ShaderCompiler, "modelAlbedo", EOS::ShaderStage::Vertex);
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(App.Context, App.ShaderCompiler, "modelAlbedo", EOS::ShaderStage::Fragment);

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

    EOS::Holder<EOS::TextureHandle> depthTexture = App.CreateDepthTexture();

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32, //TODO depthTexture->Format
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineDescription);


    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    TextureHandles handles;
    LoadModel("../data/damaged_helmet/DamagedHelmet.gltf", vertices, indices,handles, App.Context.get());

    EOS::Holder<EOS::BufferHandle> vertexBuffer = App.Context->CreateBuffer(
    {
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });

    EOS::Holder<EOS::BufferHandle> indexBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::Index,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(uint32_t) * indices.size(),
        .Data      = indices.data(),
        .DebugName = "Buffer: index"
    });

    EOS::Holder<EOS::BufferHandle> perFrameBuffer = App.Context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "perFrameBuffer",
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
            .albedoID = handles.albedo.Index(),
            .normalID = handles.normal.Index(),
            .metallicRoughnessID = handles.metallicRoughness.Index(),
        };


        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        EOS::Framebuffer framebuffer
        {
            .Color = {{.Texture = App.Context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = depthTexture },
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
                { depthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite }
            });

        cmdUpdateBuffer(cmdBuffer, perFrameBuffer, perFrameData);
        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Damaged Helmet", 0xff0000ff);
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);

            struct FramePointers
            {
                uint64_t draw;
            }pc
            {
                .draw = App.Context->GetGPUAddress(perFrameBuffer)
            };

            cmdPushConstants(cmdBuffer, pc);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexed(cmdBuffer, indices.size());
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    return 0;
}