
#include "../../Common/App.h"
#include ".generated/shadowCommon.h"


struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};


struct VertexShadow final
{
    glm::vec3 position;
};

struct Resources final
{
    EOS::ShaderProgramHolder ShadeShader;
    EOS::ShaderProgramHolder ShadowShader;
    EOS::Holder<EOS::TextureHandle> ShadowDepthTexture;
    EOS::SamplerHolder DepthMapSampler;
    EOS::Holder<EOS::BufferHandle> VertexBuffer;
    EOS::Holder<EOS::BufferHandle> IndexBuffer;
    EOS::Holder<EOS::BufferHandle> PerDrawBuffer;
    EOS::Holder<EOS::BufferHandle> PerFrameBuffer;
    EOS::Holder<EOS::BufferHandle> IndirectBuffer;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipelineHandle;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipelineShadowHandle;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipelineShadowAlphaTestedHandle;
};

Resources Handles;

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - ShadowMapping",
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

    Handles.ShadeShader = App.Context->CreateShaderProgram({.Module = "shade"});
    Handles.ShadowShader = App.Context->CreateShaderProgram({.Module = "shadowDepth"});

    //TODO: This could be constevaled with reflection
    constexpr EOS::VertexInputData vertexDesc
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


    constexpr EOS::VertexInputData vertexDescriptionShadow
    {
        // Position, plus the uv alpha-tested materials need to cut their holes into the shadow map.
        .Attributes =
        {
            { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) },
            { .Location = 1, .Format = EOS::VertexFormat::Float2, .Offset = offsetof(Vertex, uv) },
        },
        .InputBindings ={{ .Stride = sizeof(Vertex) }}
    };


    Handles.ShadowDepthTexture = App.Context->CreateTexture(
{
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {static_cast<uint32_t>(4096), static_cast<uint32_t>(4096)},
        .Usage                  = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName              = "Shadow Depth Buffer",
    });


    constexpr EOS::SamplerDescription depthMapSamplerDesc
    {
        .wrapU = EOS::SamplerWrap::ClampToBorder,
        .wrapV = EOS::SamplerWrap::ClampToBorder,
        .wrapW = EOS::SamplerWrap::ClampToBorder,
        .maxAnisotropic = 0,
        .depthCompareEnabled = false,
        .debugName = "DepthMap Sampler",
    };
    Handles.DepthMapSampler = App.Context->CreateSampler(depthMapSamplerDesc);


    Scene scene = LoadModel("../data/sponza/Sponza.gltf", App.Context.get());
    const uint32_t nOpaqueMeshes = PartitionMeshesByAlphaTest(scene);  // meshes [0, nOpaqueMeshes) are opaque, the rest alpha-tested
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

    std::vector<DrawData> drawData = BuildDrawDataFromScene<DrawData>(scene, App.DefaultSampler);

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

    Handles.IndirectBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectDrawBuffer",
    });


    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineShade
    {
        .VertexInput = vertexDesc,
        .VertexShader = {Handles.ShadeShader, "vertexMain"},
        .FragmentShader = {Handles.ShadeShader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32,
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = vertexDescriptionShadow,
        .VertexShader = {Handles.ShadowShader, "vertexMain"},
        .FragmentShader = {Handles.ShadowShader, "fragmentOpaque"},
        .DepthFormat = App.Context->GetFormat(Handles.ShadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Front,
        .DebugName = "ShadowMap Render Pipeline",
    };
    Handles.RenderPipelineShadowHandle = App.Context->CreateRenderPipeline(renderPipelineShadow);

    renderPipelineShadow.FragmentShader = {Handles.ShadowShader, "fragmentMasked"};
    renderPipelineShadow.DebugName = "ShadowMap Alpha Tested Render Pipeline";
    Handles.RenderPipelineShadowAlphaTestedHandle = App.Context->CreateRenderPipeline(renderPipelineShadow);




    //TODO: Make a abstracted movement class or something, that can either behave like projection or camera projection things.
    //Light and Camera Can implement those
    const glm::mat4 m = glm::mat4(1.0f);
    glm::vec3 lightPos          = {0.0f, 20.0f, 20.0f};
    glm::vec2 lightRotation     = {-73, -90};
    const glm::mat4 lightProjection   = glm::ortho(-25.0f, 25.0f,-25.0f, 25.0f,0.1f,50.0f);
    glm::vec3 lightUp           = {0.0f, 1.0f, 0.0f};
    const FramePointers framePointers
    {
        .perFrame = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .draws = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
    };

    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);
        if (std::isnan(aspectRatio)) return;

        glm::vec3 lightForward;
        lightForward.x = cos(glm::radians(lightRotation.y)) * cos(glm::radians(lightRotation.x));
        lightForward.y = sin(glm::radians(lightRotation.x));
        lightForward.z = sin(glm::radians(lightRotation.y)) * cos(glm::radians(lightRotation.x));
        lightForward = glm::normalize(lightForward);

        const glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightForward, lightUp);
        const glm::mat4 depthMVP = lightProjection * lightView * m;
        const glm::mat4 mvp = App.MainCamera.GetViewProjectionMatrix(aspectRatio) * m;

        const PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .depthMVP = depthMVP,
            .lightPos = glm::vec4(lightPos, 1.0f),
            .lightDir = glm::vec4(lightForward, 0.0f),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMap = Handles.ShadowDepthTexture,
            .shadowSampler = Handles.DepthMapSampler,
        };
        constexpr EOS::DepthState depthState
        {
            .CompareOpState = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };

        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
        const EOS::GraphTexture depth = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
        // Owned by the example rather than the graph: PerFrameData and the UI refer to it before the graph executes.
        const EOS::GraphTexture shadowMap = graph.ImportTexture(Handles.ShadowDepthTexture, "Shadow Map");
        const EOS::GraphBuffer perFrame = graph.ImportBuffer(Handles.PerFrameBuffer, "PerFrameBuffer");
        graph.AddUpload("Upload Frame Data", perFrame, perFrameData);

        graph.AddRasterPass("Shadow").Depth(EOS::ClearDepth(shadowMap)).Read(perFrame).Execute([&](EOS::PassContext& pass)
        {
            cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdSetDepthState(pass.Cmd, depthState);

            // Opaque meshes go through a fragment shader that never discards, which keeps the fast depth-only path.
            // Only the alpha-tested ones pay for the alpha test. Draw indices restart at 0 in the second indirect draw,
            // so its push constants point the draw data at the first alpha-tested mesh.
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipelineShadowHandle);
            cmdPushConstants(pass.Cmd, framePointers);
            cmdDrawIndexedIndirect(pass.Cmd, Handles.IndirectBuffer, 0, nOpaqueMeshes);

            const uint32_t nAlphaTestedMeshes = static_cast<uint32_t>(scene.meshes.size()) - nOpaqueMeshes;
            if (nAlphaTestedMeshes > 0)
            {
                const FramePointers alphaTestedPointers
                {
                    .perFrame = framePointers.perFrame,
                    .draws = framePointers.draws + nOpaqueMeshes * sizeof(DrawData),
                };

                cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipelineShadowAlphaTestedHandle);
                cmdPushConstants(pass.Cmd, alphaTestedPointers);
                cmdDrawIndexedIndirect(pass.Cmd, Handles.IndirectBuffer, nOpaqueMeshes * sizeof(EOS::DrawIndexedIndirectCommand), nAlphaTestedMeshes);
            }
        });

        graph.AddRasterPass("Shade")
            .Color(EOS::Clear(backbuffer, {0.36f, 0.4f, 1.0f, 0.28f}))
            .Depth(EOS::ClearDepth(depth))
            .Sample(shadowMap)
            .Read(perFrame)
            .Execute([&](EOS::PassContext& pass)
        {
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipelineHandle);
            cmdPushConstants(pass.Cmd, framePointers);
            cmdSetDepthState(pass.Cmd, depthState);
            cmdDrawIndexedIndirect(pass.Cmd, Handles.IndirectBuffer, 0, scene.meshes.size());
        });

        App.AddUIPass(backbuffer, [&]
        {
            EOS::UI::SetNextWindowSize(300, 300);
            EOS::UI::Begin("Light Settings");
            EOS::UI::DragFloat3("Light Position", glm::value_ptr(lightPos));
            EOS::UI::DragFloat2("Light Rotation", glm::value_ptr(lightRotation));
            EOS::UI::Image(EOS::UI::MakeTextureID(Handles.ShadowDepthTexture), 200, 200);
            EOS::UI::End();
        }).Sample(shadowMap);

        graph.Execute();
    });

    Handles = {};

    return 0;
}