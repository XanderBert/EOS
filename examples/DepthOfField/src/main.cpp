#include "../../Common/App.h"
#include ".generated/deferredLightCompute.h"
#include ".generated/dofBlur.h"
#include ".generated/dofComposite.h"
#include ".generated/dofDownsample.h"
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
        .ApplicationName        = "EOS - Deferred Lighting Compute",
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
    int       debugView = 0;
    float     focusDistance = 6.0f;
    float     focusRange = 3.0f;
    float     maxBlurRadius = 1.0f;

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
        const EOS::GraphTexture depth      = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
        const EOS::GraphTexture albedo     = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "GBuffer AlbedoMetallic"});
        const EOS::GraphTexture normal     = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "GBuffer NormalRoughness"});
        const EOS::GraphTexture worldPos   = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .DebugName = "GBuffer WorldPosition"});
        const EOS::GraphTexture lit        = graph.CreateTexture({.TextureFormat = App.Context->GetSwapchainFormat(), .DebugName = "Deferred Lit Scene"});
        const EOS::GraphTexture dofHalf    = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .Scale = 0.5f, .DebugName = "DOF Half"});
        const EOS::GraphTexture dofBlurH   = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .Scale = 0.5f, .DebugName = "DOF Blur H"});
        const EOS::GraphTexture dofBlurred = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .Scale = 0.5f, .DebugName = "DOF Blurred"});
        const EOS::GraphTexture dof        = graph.CreateTexture({.TextureFormat = App.Context->GetSwapchainFormat(), .DebugName = "DOF Output"});
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

        graph.AddComputePass("Deferred Lighting").Sample({albedo, normal, worldPos}).Write(lit).Execute([&](EOS::PassContext& pass)
        {
            cmdBindComputePipeline(pass.Cmd, Handles.DeferredLightingPipeline);
            cmdPushConstants(pass.Cmd, DeferredLightingPC
            {
                .gbufferAlbedo     = pass.Descriptor(albedo),
                .gbufferNormal     = pass.Descriptor(normal),
                .gbufferWorldPos   = pass.Descriptor(worldPos),
                .samplerState      = App.DefaultSampler,
                .outputImage       = pass.Descriptor(lit),
                .debugView         = static_cast<uint32_t>(debugView),
                .cameraPos         = glm::vec4(App.MainCamera.GetPosition(), 1.0f),
                .lightDirIntensity = glm::vec4(lightDirection, lightIntensity),
                .lightColorAmbient = glm::vec4(lightColor, kAmbientRadiance),
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(lit));
        });

        graph.AddComputePass("DOF Downsample").Sample({lit, worldPos}).Write(dofHalf).Execute([&](EOS::PassContext& pass)
        {
            cmdBindComputePipeline(pass.Cmd, Handles.DofDownsamplePipeline);
            cmdPushConstants(pass.Cmd, DofDownsamplePC
            {
                .sceneColor    = pass.Descriptor(lit),
                .worldPos      = pass.Descriptor(worldPos),
                .samplerState  = App.DefaultSampler,
                .outputImage   = pass.Descriptor(dofHalf),
                .focusDistance = focusDistance,
                .focusRange    = focusRange,
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(dofHalf));
        });

        // Separable blur: horizontal, then vertical.
        const auto addBlurPass = [&](const char* name, EOS::GraphTexture input, EOS::GraphTexture output, EOS::ComputePipelineHandle pipeline)
        {
            graph.AddComputePass(name).Sample(input).Write(output).Execute([&, input, output, pipeline](EOS::PassContext& pass)
            {
                const EOS::Dimensions size = pass.Size(output);
                cmdBindComputePipeline(pass.Cmd, pipeline);
                cmdPushConstants(pass.Cmd, DofBlurPC
                {
                    .inputImage    = pass.Descriptor(input),
                    .outputImage   = pass.Descriptor(output),
                    .samplerState  = App.DefaultSampler,
                    .maxBlurRadius = maxBlurRadius,
                    .texelSizeX    = 1.0f / static_cast<float>(size.Width),
                    .texelSizeY    = 1.0f / static_cast<float>(size.Height),
                });
                cmdDispatchThreads(pass.Cmd, size);
            });
        };
        addBlurPass("DOF Blur H", dofHalf, dofBlurH, Handles.DofBlurHPipeline);
        addBlurPass("DOF Blur V", dofBlurH, dofBlurred, Handles.DofBlurVPipeline);

        graph.AddComputePass("DOF Composite").Sample({lit, dofBlurred, worldPos}).Write(dof).Execute([&](EOS::PassContext& pass)
        {
            cmdBindComputePipeline(pass.Cmd, Handles.DofCompositePipeline);
            cmdPushConstants(pass.Cmd, DofCompositePC
            {
                .sceneColor    = pass.Descriptor(lit),
                .blurred       = pass.Descriptor(dofBlurred),
                .worldPos      = pass.Descriptor(worldPos),
                .samplerState  = App.DefaultSampler,
                .outputImage   = pass.Descriptor(dof),
                .focusDistance = focusDistance,
                .focusRange    = focusRange,
                .maxBlurRadius = maxBlurRadius,
            });
            cmdDispatchThreads(pass.Cmd, pass.Size(dof));
        });

        graph.AddRasterPass("Present").Color(EOS::Clear(backbuffer, {0.02f, 0.02f, 0.02f, 1.0f})).Sample(dof).Execute([&](EOS::PassContext& pass)
        {
            cmdBindRenderPipeline(pass.Cmd, Handles.PresentPipeline);
            cmdPushConstants(pass.Cmd, PresentPC{.sceneColor = pass.Descriptor(dof), .samplerState = App.DefaultSampler});
            cmdDraw(pass.Cmd, 3);
        });

        App.AddUIPass(backbuffer, [&]
        {
            EOS::UI::SetNextWindowSize(420, 360);
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
            constexpr const char* debugModes[] = {"Lit", "Albedo", "Normals", "Roughness", "World Position"};
            EOS::UI::Combo("Debug view", &debugView, debugModes);
            EOS::UI::End();
        });

        graph.Execute();
    });

    Handles = {};

    return 0;
}