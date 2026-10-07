#include "../../Common/App.h"
#include ".generated/deferredLight.h"
#include ".generated/edgeDetect.h"
#include ".generated/indirectModel.h"

// Radiance of the uniform environment that stands in for image-based lighting.
constexpr float kAmbientRadiance = 0.3f;

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Resources final
{
    EOS::ShaderProgramHolder ModelShader;
    EOS::ShaderProgramHolder DeferredLightShader;
    EOS::ShaderProgramHolder EdgeDetectShader;
    EOS::BufferHolder VertexBuffer;
    EOS::BufferHolder IndexBuffer;
    EOS::BufferHolder PerDrawBuffer;
    EOS::BufferHolder PerFrameBuffer;
    EOS::BufferHolder IndirectDrawBuffer;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipeline;
    EOS::Holder<EOS::RenderPipelineHandle> DeferredLightingPipeline;
    EOS::Holder<EOS::RenderPipelineHandle> EdgePipeline;
};

Resources Handles;

int main()
{
    const EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Deferred EdgeDetection",
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

    Handles.ModelShader = App.Context->CreateShaderProgram({.Module = "indirectModel"});
    Handles.DeferredLightShader = App.Context->CreateShaderProgram({.Module = "deferredLight"});
    Handles.EdgeDetectShader = App.Context->CreateShaderProgram({.Module = "edgeDetect"});
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

    Handles.VertexBuffer = App.Context->CreateBuffer({
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });

    Handles.IndexBuffer = App.Context->CreateBuffer({
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

    Handles.IndirectDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectDrawBuffer",
    });

    const EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = {Handles.ModelShader, "vertexMain"},
        .FragmentShader = {Handles.ModelShader, "fragmentMain"},
        .ColorAttachments =
        {
            { .ColorFormat = EOS::Format::RGBA_UN8 },
            { .ColorFormat = EOS::Format::RGBA_UN8 },
            { .ColorFormat = EOS::Format::RGBA_F16 },
        },
        .DepthFormat = EOS::Format::Z_F32,
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Deferred Geometry Pipeline",
    };
    Handles.RenderPipeline = App.Context->CreateRenderPipeline(renderPipelineDescription);

    const EOS::RenderPipelineDescription deferredLightingPipelineDescription
    {
        .VertexShader     = {Handles.DeferredLightShader, "vertexMain"},
        .FragmentShader   = {Handles.DeferredLightShader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat() }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Deferred Lighting Pipeline",
    };
    Handles.DeferredLightingPipeline = App.Context->CreateRenderPipeline(deferredLightingPipelineDescription);

    const EOS::RenderPipelineDescription edgePipelineDescription
    {
        .VertexShader     = {Handles.EdgeDetectShader, "vertexMain"},
        .FragmentShader   = {Handles.EdgeDetectShader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat() }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Edge Detect Pipeline",
    };
    Handles.EdgePipeline = App.Context->CreateRenderPipeline(edgePipelineDescription);

    const FramePointers framePointers
    {
        .perFrame = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .draws = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
    };

    glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    float     lightIntensity = 1.0f;
    glm::vec3 lightColor{1.0f, 0.98f, 0.92f};
    float     edgeThreshold = 4.0f;
    bool      showEdgesOnly = false;
    int       debugView = 0;

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


        constexpr EOS::DepthState depthState
        {
            .CompareOpState      = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };

        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
        const EOS::GraphTexture depth    = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
        const EOS::GraphTexture albedo   = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "GBuffer AlbedoMetallic"});
        const EOS::GraphTexture normal   = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "GBuffer NormalRoughness"});
        const EOS::GraphTexture worldPos = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .DebugName = "GBuffer WorldPosition"});
        const EOS::GraphTexture lit      = graph.CreateTexture({.TextureFormat = App.Context->GetSwapchainFormat(), .DebugName = "Deferred Lit Scene"});
        const EOS::GraphBuffer perFrame = graph.ImportBuffer(Handles.PerFrameBuffer, "PerFrameBuffer");
        graph.AddUpload("Upload Frame Data", perFrame, perFrameData);

        graph.AddRasterPass("Geometry")
            .Color(EOS::Clear(albedo, {0.36f, 0.4f, 1.0f, 1.0f}))
            .Color(EOS::Clear(normal, {0.5f, 0.5f, 1.0f, 1.0f}))
            .Color(EOS::Clear(worldPos, {0.0f, 0.0f, 0.0f, 1.0f}))
            .Depth(EOS::ClearDepth(depth))
            .Read(perFrame)
            .Execute([&](EOS::PassContext& pass)
        {
            cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipeline);
            cmdPushConstants(pass.Cmd, framePointers);
            cmdSetDepthState(pass.Cmd, depthState);
            cmdDrawIndexedIndirect(pass.Cmd, Handles.IndirectDrawBuffer, 0, scene.meshes.size());
        });

        graph.AddRasterPass("Deferred Lighting").Color(EOS::Clear(lit)).Sample({albedo, normal, worldPos}).Execute([&](EOS::PassContext& pass)
        {
            cmdBindRenderPipeline(pass.Cmd, Handles.DeferredLightingPipeline);
            cmdPushConstants(pass.Cmd, DeferredLightingPC
            {
                .gbufferAlbedo     = pass.Descriptor(albedo),
                .gbufferNormal     = pass.Descriptor(normal),
                .gbufferWorldPos   = pass.Descriptor(worldPos),
                .samplerState      = App.DefaultSampler,
                .debugView         = static_cast<uint32_t>(debugView),
                .cameraPos         = glm::vec4(App.MainCamera.GetPosition(), 1.0f),
                .lightDirIntensity = glm::vec4(lightDirection, lightIntensity),
                .lightColorAmbient = glm::vec4(lightColor, kAmbientRadiance),
            });
            cmdDraw(pass.Cmd, 3);
        });

        graph.AddRasterPass("Edge Detect").Color(EOS::Clear(backbuffer)).Sample({lit, normal}).Execute([&](EOS::PassContext& pass)
        {
            const EOS::Dimensions size = pass.Size(lit);
            cmdBindRenderPipeline(pass.Cmd, Handles.EdgePipeline);
            cmdPushConstants(pass.Cmd, EdgeDetectPC
            {
                .sceneColor    = pass.Descriptor(lit),
                .sceneNormal   = pass.Descriptor(normal),
                .samplerState  = App.DefaultSampler,
                .threshold     = edgeThreshold,
                .showEdgesOnly = showEdgesOnly ? 1u : 0u,
                .texelW        = 1.0f / static_cast<float>(size.Width),
                .texelH        = 1.0f / static_cast<float>(size.Height),
            });
            cmdDraw(pass.Cmd, 3);
        });

        App.AddUIPass(backbuffer, [&]
        {
            EOS::UI::SetNextWindowSize(360, 220);
            EOS::UI::Begin("Deferred Edge Detection");
            EOS::UI::SliderFloat3("Light direction", &lightDirection.x, -1.0f, 1.0f);
            if (glm::length(lightDirection) > 0.0001f)
            {
                lightDirection = glm::normalize(lightDirection);
            }
            EOS::UI::SliderFloat("Light intensity", &lightIntensity, 0.0f, 2.0f);
            EOS::UI::ColorEdit3("Light color", &lightColor.x);
            EOS::UI::Separator();
            EOS::UI::SliderFloat("Edge threshold", &edgeThreshold, 2.0f, 8.0f);
            EOS::UI::Checkbox("Edges only", &showEdgesOnly);
            constexpr const char* debugModes[] = {"Lit", "Albedo", "Normals", "Roughness", "World Position"};
            EOS::UI::Combo("Debug view", &debugView, debugModes);
            EOS::UI::End();
        });

        graph.Execute();
    });

    Handles = {};

    return 0;
}