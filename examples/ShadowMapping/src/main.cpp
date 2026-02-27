#include "../../common.h"
#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"
#include "utils.h"

struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;
    glm::mat4 depthMVP;
    glm::vec4 lightPos;
    glm::vec3 cameraPos;
    uint32_t  shadowMapID;
    uint64_t  drawDataPtr;
};

struct DrawData final
{
    uint32_t albedoID;
    uint32_t normalID;
    uint32_t metallicRoughnessID;
    uint32_t pad;
};

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - ShadowMapping",
    };

    CameraDescription cameraDescription
    {
        .origin = {0.0f, 10.0f, -3.5f},
        .rotation = {0, 0.0f},
        .acceleration = 200.0f
    };

    ExampleAppDescription appDescription
    {
        .contextDescription = contextDescr,
        .cameraDescription = cameraDescription
    };

    ExampleApp App{appDescription};

    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(App.Context, App.ShaderCompiler, "modelAlbedo", EOS::ShaderStage::Vertex);
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(App.Context, App.ShaderCompiler, "modelAlbedo", EOS::ShaderStage::Fragment);

    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleShadowVert = EOS::LoadShader(App.Context, App.ShaderCompiler, "shadowDepth", EOS::ShaderStage::Vertex);
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleShadowFrag = EOS::LoadShader(App.Context, App.ShaderCompiler, "shadowDepth", EOS::ShaderStage::Fragment);

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

    EOS::Holder<EOS::TextureHandle> shadowDepthTexture = App.Context->CreateTexture(
{
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {2048, 2048},
        .Usage                  = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName              = "Depth Buffer",
    });


    Scene scene = LoadModel("../data/sponza/Sponza.gltf", App.Context.get());
    std::vector<Vertex> vertices;
    vertices.reserve(scene.vertices.size());
    for (const VertexInformation& vertexInfo : scene.vertices)  vertices.emplace_back(vertexInfo.position, vertexInfo.normal, vertexInfo.uv, vertexInfo.tangent);


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
        .Size      = sizeof(uint32_t) * scene.indices.size(),
        .Data      = scene.indices.data(),
        .DebugName = "Buffer: index"
    });

    std::vector<DrawData> drawData;
    drawData.reserve(scene.meshes.size());
    for (auto& mesh : scene.meshes)
    {
        drawData.push_back({
            .albedoID            = mesh.textures.albedo.Index(),
            .normalID            = mesh.textures.normal.Index(),
            .metallicRoughnessID = mesh.textures.metallicRoughness.Index(),
        });
    }

    EOS::Holder<EOS::BufferHandle> drawDataBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "drawDataBuffer",
    });

    EOS::Holder<EOS::BufferHandle> perFrameBuffer = App.Context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "perFrameBuffer",
    });

    std::vector<EOS::DrawIndexedIndirectCommand> indirectCmds;
    indirectCmds.reserve(scene.meshes.size());
    for (auto& mesh : scene.meshes)
    {
        indirectCmds.emplace_back(
        EOS::DrawIndexedIndirectCommand
        {
            .indexCount    = mesh.indexCount,
            .instanceCount = 1,
            .firstIndex    = mesh.indexOffset,
            .vertexOffset  = static_cast<int32_t>(mesh.vertexOffset),
            .firstInstance = 0,
        });
    }

    EOS::Holder<EOS::BufferHandle> indirectBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "indirectBuffer",
    });


    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineShade
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32, //TODO depthTexture->Format
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleShadowVert,
        .FragmentShader = shaderHandleShadowFrag,
        .ColorAttachments = {},
        .DepthFormat = EOS::Format::Z_F32, //TODO depthTexture->Format
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "ShadowMap Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineShadowHandle = App.Context->CreateRenderPipeline(renderPipelineShadow);


    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(0.04f));
    constexpr glm::vec3 lightPos = glm::vec3(10.0f, 10.0f, 0.0f);


    // Matrix from light's point of view
    constexpr float lightFOV = 90.0f;
    constexpr float zNear = 0.01f;
    constexpr float zFar = 100.0f;
    const glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
    const glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const glm::mat4 depthModelMatrix = m;//glm::mat4(1.0f);
    const glm::mat4 depthMVP = depthProjectionMatrix * depthViewMatrix;



    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);


        const glm::mat4 mvp = App.MainCamera.GetViewProjectionMatrix(aspectRatio) * m;

        const PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .depthMVP = depthMVP,
            .lightPos = glm::vec4(lightPos, 1.0f),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMapID = shadowDepthTexture.Index(),
            .drawDataPtr = App.Context->GetGPUAddress(drawDataBuffer)
        };

        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();


        EOS::Framebuffer framebufferShadow
        {
            .DepthStencil = { .Texture = shadowDepthTexture },
            .DebugName = "ShadowMap framebuffer"
        };


        EOS::Framebuffer framebufferShade
        {
            .Color = {{.Texture = App.Context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = depthTexture },
            .DebugName = "Basic Color Depth Framebuffer",
        };

        constexpr EOS::RenderPass renderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
            .Depth{ .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
        };

        constexpr EOS::RenderPass shadowRenderPass
        {
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
                { depthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
                { shadowDepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
            });

        cmdUpdateBuffer(cmdBuffer, perFrameBuffer, perFrameData);

        struct FramePointers
        {
            uint64_t draw;
        }pc
        {
            .draw = App.Context->GetGPUAddress(perFrameBuffer)
        };

        cmdPushMarker(cmdBuffer, "Shadow Pass", 0xff0000ff);
        cmdBeginRendering(cmdBuffer, shadowRenderPass, framebufferShadow);
        {
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineShadowHandle);
            cmdPushConstants(cmdBuffer, pc);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, indirectBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        //Transition Shadow Depth to Shader Resource
        cmdPipelineBarrier(cmdBuffer, {},{{ shadowDepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },});

        cmdPushMarker(cmdBuffer, "Shade Pass", 0xff0000ff);
        cmdBeginRendering(cmdBuffer, renderPass, framebufferShade);
        {
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdPushConstants(cmdBuffer, pc);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, indirectBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    return 0;
}