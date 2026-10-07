#include "../../Common/App.h"
#include ".generated/deferredLightCompute.h"
#include ".generated/dofBlur.h"
#include ".generated/dofComposite.h"
#include ".generated/dofDownsample.h"
#include ".generated/graphs/depthOfField.h"
#include ".generated/indirectModel.h"
#include ".generated/present.h"

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

// Radiance of the uniform environment that stands in for image-based lighting.
constexpr float kAmbientRadiance = 0.3f;

struct Resources final
{
    EOS::ShaderProgramHolder ModelShader;
    EOS::ShaderProgramHolder DeferredLightComputeShader;
    EOS::ShaderProgramHolder DofDownsampleShader;
    EOS::ShaderProgramHolder DofBlurShader;
    EOS::ShaderProgramHolder DofCompositeShader;
    EOS::ShaderProgramHolder PresentShader;
    EOS::BufferHolder VertexBuffer;
    EOS::BufferHolder IndexBuffer;
    EOS::BufferHolder PerDrawBuffer;
    EOS::BufferHolder PerFrameBuffer;
    EOS::BufferHolder IndirectDrawBuffer;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipeline;
    EOS::Holder<EOS::ComputePipelineHandle> DeferredLightingPipeline;
    EOS::Holder<EOS::ComputePipelineHandle> DofDownsamplePipeline;
    EOS::Holder<EOS::ComputePipelineHandle> DofBlurHPipeline;
    EOS::Holder<EOS::ComputePipelineHandle> DofBlurVPipeline;
    EOS::Holder<EOS::ComputePipelineHandle> DofCompositePipeline;
    EOS::Holder<EOS::RenderPipelineHandle> PresentPipeline;
};

Resources Handles;

int main()
{
    const EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Depth Of Field",
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
    Handles.DeferredLightComputeShader = App.Context->CreateShaderProgram({.Module = "deferredLightCompute"});
    Handles.DofDownsampleShader = App.Context->CreateShaderProgram({.Module = "dofDownsample"});
    Handles.DofBlurShader = App.Context->CreateShaderProgram({.Module = "dofBlur"});
    Handles.DofCompositeShader = App.Context->CreateShaderProgram({.Module = "dofComposite"});
    Handles.PresentShader = App.Context->CreateShaderProgram({.Module = "present"});
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

    const EOS::ComputePipelineDescription deferredLightingPipelineDescription
    {
        .ComputeShader = {Handles.DeferredLightComputeShader, "computeMain"},
        .DebugName     = "Deferred Lighting Compute Pipeline",
    };
    Handles.DeferredLightingPipeline = App.Context->CreateComputePipeline(deferredLightingPipelineDescription);

    const EOS::ComputePipelineDescription dofDownsamplePipelineDescription
    {
        .ComputeShader = {Handles.DofDownsampleShader, "computeMain"},
        .DebugName     = "DOF Downsample Pipeline",
    };
    Handles.DofDownsamplePipeline = App.Context->CreateComputePipeline(dofDownsamplePipelineDescription);

    const EOS::ComputePipelineDescription dofBlurHPipelineDescription
    {
        .ComputeShader = {Handles.DofBlurShader, "blurHorizontal"},
        .DebugName     = "DOF Blur H Pipeline",
    };
    Handles.DofBlurHPipeline = App.Context->CreateComputePipeline(dofBlurHPipelineDescription);

    const EOS::ComputePipelineDescription dofBlurVPipelineDescription
    {
        .ComputeShader = {Handles.DofBlurShader, "blurVertical"},
        .DebugName     = "DOF Blur V Pipeline",
    };
    Handles.DofBlurVPipeline = App.Context->CreateComputePipeline(dofBlurVPipelineDescription);

    const EOS::ComputePipelineDescription dofCompositePipelineDescription
    {
        .ComputeShader = {Handles.DofCompositeShader, "computeMain"},
        .DebugName     = "DOF Composite Pipeline",
    };
    Handles.DofCompositePipeline = App.Context->CreateComputePipeline(dofCompositePipelineDescription);

    const EOS::RenderPipelineDescription presentPipelineDescription
    {
        .VertexShader     = {Handles.PresentShader, "vertexMain"},
        .FragmentShader   = {Handles.PresentShader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat() }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Present Pipeline",
    };
    Handles.PresentPipeline = App.Context->CreateRenderPipeline(presentPipelineDescription);

    const FramePointers framePointers
    {
        .perFrame = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .draws = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
    };

    glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    float     lightIntensity = 1.0f;
    glm::vec3 lightColor{1.0f, 0.98f, 0.92f};
    float     focusDistance = 6.0f;
    float     focusRange = 3.0f;
    float     maxBlurRadius = 1.0f;

    // The pass types graphs/depthOfField.yaml is built from. The file decides which passes run and how they connect.
    App.Passes.Register(
    {
        .Name = "GBuffer",
        .Kind = EOS::PassKind::Raster,
        .Pins =
        {
            EOS::Pin::ReadBuffer("perFrame"),
            EOS::Pin::ColorOutput("albedo", EOS::Format::RGBA_UN8, {.ClearColor = {0.36f, 0.4f, 1.0f, 1.0f}}),
            EOS::Pin::ColorOutput("normal", EOS::Format::RGBA_UN8, {.ClearColor = {0.5f, 0.5f, 1.0f, 1.0f}}),
            EOS::Pin::ColorOutput("worldPos", EOS::Format::RGBA_F16, {.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f}}),
            EOS::Pin::DepthOutput("depth", EOS::Format::Z_F32),
        },
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData&)
        {
            constexpr EOS::DepthState depthState{.CompareOpState = EOS::CompareOp::Less, .IsDepthWriteEnabled = true};

            cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(pass.Cmd, Handles.RenderPipeline);
            cmdPushConstants(pass.Cmd, framePointers);
            cmdSetDepthState(pass.Cmd, depthState);
            cmdDrawIndexedIndirect(pass.Cmd, Handles.IndirectDrawBuffer, 0, scene.meshes.size());
        },
    });

    static constexpr const char* kDebugViews[] = {"Lit", "Albedo", "Normals", "Roughness", "World Position"};
    App.Passes.Register(
    {
        .Name = "DeferredLighting",
        .Kind = EOS::PassKind::Compute,
        .Pins =
        {
            EOS::Pin::Sampled("albedo"),
            EOS::Pin::Sampled("normal"),
            EOS::Pin::Sampled("worldPos"),
            EOS::Pin::StorageOutput("output", EOS::Pin::SwapchainFormat),
        },
        .Properties = {EOS::Property::Choice("debugView", kDebugViews)},
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData& data)
        {
            const EOS::GraphTexture output = data.Texture("output");
            cmdBindComputePipeline(pass.Cmd, Handles.DeferredLightingPipeline);
            cmdPushConstants(pass.Cmd, DeferredLightingPC
            {
                .gbufferAlbedo     = pass.Descriptor(data.Texture("albedo")),
                .gbufferNormal     = pass.Descriptor(data.Texture("normal")),
                .gbufferWorldPos   = pass.Descriptor(data.Texture("worldPos")),
                .samplerState      = App.DefaultSampler,
                .outputImage       = pass.Descriptor(output),
                .debugView         = static_cast<uint32_t>(data.Int("debugView")),
                .cameraPos         = glm::vec4(App.MainCamera.GetPosition(), 1.0f),
                .lightDirIntensity = glm::vec4(lightDirection, lightIntensity),
                .lightColorAmbient = glm::vec4(lightColor, kAmbientRadiance),
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(output));
        },
    });

    App.Passes.Register(
    {
        .Name = "DofDownsample",
        .Kind = EOS::PassKind::Compute,
        .Pins =
        {
            EOS::Pin::Sampled("color"),
            EOS::Pin::Sampled("worldPos"),
            EOS::Pin::StorageOutput("output", EOS::Format::RGBA_F16, {.Scale = 0.5f, .SizeOf = "color"}),
        },
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData& data)
        {
            const EOS::GraphTexture output = data.Texture("output");
            cmdBindComputePipeline(pass.Cmd, Handles.DofDownsamplePipeline);
            cmdPushConstants(pass.Cmd, DofDownsamplePC
            {
                .sceneColor    = pass.Descriptor(data.Texture("color")),
                .worldPos      = pass.Descriptor(data.Texture("worldPos")),
                .samplerState  = App.DefaultSampler,
                .outputImage   = pass.Descriptor(output),
                .focusDistance = focusDistance,
                .focusRange    = focusRange,
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(output));
        },
    });

    // One direction of the separable blur.
    static constexpr const char* kBlurDirections[] = {"Horizontal", "Vertical"};
    App.Passes.Register(
    {
        .Name = "DofBlur",
        .Kind = EOS::PassKind::Compute,
        .Pins =
        {
            EOS::Pin::Sampled("input"),
            EOS::Pin::StorageOutput("output", EOS::Format::RGBA_F16, {.SizeOf = "input", .BypassFrom = "input"}),
        },
        .Properties = {EOS::Property::Choice("direction", kBlurDirections)},
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData& data)
        {
            const EOS::GraphTexture output = data.Texture("output");
            const EOS::Dimensions size = pass.Size(output);
            cmdBindComputePipeline(pass.Cmd, data.Int("direction") == 0 ? Handles.DofBlurHPipeline : Handles.DofBlurVPipeline);
            cmdPushConstants(pass.Cmd, DofBlurPC
            {
                .inputImage    = pass.Descriptor(data.Texture("input")),
                .outputImage   = pass.Descriptor(output),
                .samplerState  = App.DefaultSampler,
                .maxBlurRadius = maxBlurRadius,
                .texelSizeX    = 1.0f / static_cast<float>(size.Width),
                .texelSizeY    = 1.0f / static_cast<float>(size.Height),
            });
            cmdDispatchThreads(pass.Cmd, size);
        },
    });

    App.Passes.Register(
    {
        .Name = "DofComposite",
        .Kind = EOS::PassKind::Compute,
        .Pins =
        {
            EOS::Pin::Sampled("color"),
            EOS::Pin::Sampled("blurred"),
            EOS::Pin::Sampled("worldPos"),
            EOS::Pin::StorageOutput("output", EOS::Pin::SwapchainFormat, {.BypassFrom = "color"}),
        },
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData& data)
        {
            const EOS::GraphTexture output = data.Texture("output");
            cmdBindComputePipeline(pass.Cmd, Handles.DofCompositePipeline);
            cmdPushConstants(pass.Cmd, DofCompositePC
            {
                .sceneColor    = pass.Descriptor(data.Texture("color")),
                .blurred       = pass.Descriptor(data.Texture("blurred")),
                .worldPos      = pass.Descriptor(data.Texture("worldPos")),
                .samplerState  = App.DefaultSampler,
                .outputImage   = pass.Descriptor(output),
                .focusDistance = focusDistance,
                .focusRange    = focusRange,
                .maxBlurRadius = maxBlurRadius,
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(output));
        },
    });

    App.Passes.Register(
    {
        .Name = "Present",
        .Kind = EOS::PassKind::Raster,
        .Pins =
        {
            EOS::Pin::Sampled("input"),
            EOS::Pin::ColorOutput("output", EOS::Pin::SwapchainFormat, {.ClearColor = {0.02f, 0.02f, 0.02f, 1.0f}}),
        },
        .Execute = [&](EOS::PassContext& pass, const EOS::PassData& data)
        {
            cmdBindRenderPipeline(pass.Cmd, Handles.PresentPipeline);
            cmdPushConstants(pass.Cmd, PresentPC{.sceneColor = pass.Descriptor(data.Texture("input")), .samplerState = App.DefaultSampler});
            cmdDraw(pass.Cmd, 3);
        },
    });

    EOS::GraphFile& graphFile = App.LoadGraphFile(DepthOfFieldGraph);

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

        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
        const EOS::GraphBuffer perFrame = graph.ImportBuffer(Handles.PerFrameBuffer, "PerFrameBuffer");
        graph.AddUpload("Upload Frame Data", perFrame, perFrameData);

        graphFile.AddTo(graph, {{"swapchain", backbuffer}, {"perFrame", perFrame}});

        App.AddUIPass(backbuffer, [&]
        {
            EOS::UI::SetNextWindowSize(420, 560);
            EOS::UI::Begin("Deferred Lighting + DOF");
            EOS::UI::SliderFloat3("Light direction", &lightDirection.x, -1.0f, 1.0f);
            if (glm::length(lightDirection) > 0.0001f)
            {
                lightDirection = glm::normalize(lightDirection);
            }
            EOS::UI::SliderFloat("Light intensity", &lightIntensity, 0.0f, 2.0f);
            EOS::UI::ColorEdit3("Light color", &lightColor.x);
            EOS::UI::Separator();
            EOS::UI::SliderFloat("Focus distance", &focusDistance, 0.1f, 25.0f);
            EOS::UI::SliderFloat("Focus range", &focusRange, 0.1f, 20.0f);
            EOS::UI::SliderFloat("Max blur radius", &maxBlurRadius, 0.0f, 12.0f);
            EOS::UI::Separator();
            EOS::UI::Text("Render graph (graphs/depthOfField.yaml)");
            EOS::UI::GraphFileProperties(graphFile);
            EOS::UI::End();
        });

        graph.Execute();
    });

    Handles = {};

    return 0;
}
