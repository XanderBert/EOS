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

struct DeferredLightingPC final
{
    uint32_t gbufferAlbedoID;
    uint32_t gbufferNormalID;
    uint32_t gbufferWorldPosID;
    uint32_t samplerID;
    uint32_t outputImageID;
    uint32_t debugView;
    uint32_t pad0;
    uint32_t pad1;
    glm::vec4 cameraPos;
    glm::vec4 lightDirIntensity;
    glm::vec4 lightColorAmbient;
};

struct PresentPC final
{
    uint32_t sceneColorID;
    uint32_t samplerID;
    uint32_t pad0;
    uint32_t pad1;
};

struct DofDownsamplePC final
{
    uint32_t sceneColorID;
    uint32_t worldPosID;
    uint32_t samplerID;
    uint32_t outputImageID;
    float    focusDistance;
    float    focusRange;
    float    pad0;
    float    pad1;
};

struct DofBlurPC final
{
    uint32_t inputImageID;
    uint32_t outputImageID;
    uint32_t samplerID;
    float    maxBlurRadius;
    float    texelSizeX;
    float    texelSizeY;
    float    pad0;
    float    pad1;
};

struct DofCompositePC final
{
    uint32_t sceneColorID;
    uint32_t blurredID;
    uint32_t worldPosID;
    uint32_t samplerID;
    uint32_t outputImageID;
    float    focusDistance;
    float    focusRange;
    float    maxBlurRadius;
};

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

    EOS::ShaderModuleHolder vertexShader = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder pixelShader  = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Fragment);
    EOS::ShaderModuleHolder deferredLightComputeShader = App.Context->CreateShaderModule("deferredLightCompute", EOS::ShaderStage::Compute);
    EOS::ShaderModuleHolder dofDownsampleShader = App.Context->CreateShaderModule("dofDownsample", EOS::ShaderStage::Compute);
    EOS::ShaderModuleHolder dofBlurHShader = App.Context->CreateShaderModule("dofBlurH", EOS::ShaderStage::Compute);
    EOS::ShaderModuleHolder dofBlurVShader = App.Context->CreateShaderModule("dofBlurV", EOS::ShaderStage::Compute);
    EOS::ShaderModuleHolder dofCompositeShader = App.Context->CreateShaderModule("dofComposite", EOS::ShaderStage::Compute);
    EOS::ShaderModuleHolder presentVertShader = App.Context->CreateShaderModule("present", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder presentFragShader = App.Context->CreateShaderModule("present", EOS::ShaderStage::Fragment);
    EOS::TextureHolder depthTexture = App.CreateDepthTexture();

    EOS::TextureHolder gbufferAlbedoTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = EOS::Format::RGBA_UN8,
        .TextureDimensions= { static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height) },
        .Usage            = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName        = "GBuffer AlbedoMetallic",
    });

    EOS::TextureHolder gbufferNormalTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = EOS::Format::RGBA_UN8,
        .TextureDimensions= { static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height) },
        .Usage            = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName        = "GBuffer NormalRoughness",
    });

    EOS::TextureHolder gbufferWorldPosTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = EOS::Format::RGBA_F16,
        .TextureDimensions= { static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height) },
        .Usage            = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName        = "GBuffer WorldPosition",
    });

    EOS::TextureHolder sceneLitTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = App.Context->GetSwapchainFormat(),
        .TextureDimensions= { static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height) },
        .Usage            = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled | EOS::TextureUsageFlags::Storage,
        .DebugName        = "Deferred Lit Scene",
    });

    const uint32_t halfWidth = (static_cast<uint32_t>(App.Window.Width) + 1) / 2;
    const uint32_t halfHeight = (static_cast<uint32_t>(App.Window.Height) + 1) / 2;

    EOS::TextureHolder dofHalfTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = EOS::Format::RGBA_F16,
        .TextureDimensions= { halfWidth, halfHeight },
        .Usage            = EOS::TextureUsageFlags::Sampled | EOS::TextureUsageFlags::Storage,
        .DebugName        = "DOF Half",
    });

    EOS::TextureHolder dofBlurTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = EOS::Format::RGBA_F16,
        .TextureDimensions= { halfWidth, halfHeight },
        .Usage            = EOS::TextureUsageFlags::Sampled | EOS::TextureUsageFlags::Storage,
        .DebugName        = "DOF Blur",
    });

    EOS::TextureHolder dofTexture = App.Context->CreateTexture({
        .Type             = EOS::ImageType::Image_2D,
        .TextureFormat    = App.Context->GetSwapchainFormat(),
        .TextureDimensions= { static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height) },
        .Usage            = EOS::TextureUsageFlags::Sampled | EOS::TextureUsageFlags::Storage,
        .DebugName        = "DOF Output",
    });

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
    std::vector<Vertex> vertices;
    vertices.reserve(scene.vertices.size());
    for (const VertexInformation& vertexInfo : scene.vertices)  vertices.emplace_back(vertexInfo.position, vertexInfo.normal, vertexInfo.uv, vertexInfo.tangent);

    EOS::BufferHolder vertexBuffer = App.Context->CreateBuffer({
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });

    EOS::BufferHolder indexBuffer = App.Context->CreateBuffer({
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
            .albedoID            = mesh.albedoTextureIdx,
            .normalID            = mesh.normalTextureIdx,
            .metallicRoughnessID = mesh.metallicRoughnessTextureIdx,
            .transform           = mesh.transform,
        });
    }

    EOS::BufferHolder perDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "PerDrawBuffer",
    });

    EOS::BufferHolder perFrameBuffer = App.Context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "PerFrameBuffer",
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

    EOS::BufferHolder indirectDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectDrawBuffer",
    });

    const EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = vertexShader,
        .FragmentShader = pixelShader,
        .ColorAttachments =
        {
            { .ColorFormat = EOS::Format::RGBA_UN8 },
            { .ColorFormat = EOS::Format::RGBA_UN8 },
            { .ColorFormat = EOS::Format::RGBA_F16 },
        },
        .DepthFormat = App.Context->GetFormat(depthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Deferred Geometry Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineDescription);

    const EOS::ComputePipelineDescription deferredLightingPipelineDescription
    {
        .ComputeShader = deferredLightComputeShader,
        .DebugName     = "Deferred Lighting Compute Pipeline",
    };
    EOS::Holder<EOS::ComputePipelineHandle> deferredLightingPipelineHandle = App.Context->CreateComputePipeline(deferredLightingPipelineDescription);

    const EOS::ComputePipelineDescription dofDownsamplePipelineDescription
    {
        .ComputeShader = dofDownsampleShader,
        .DebugName     = "DOF Downsample Pipeline",
    };
    EOS::Holder<EOS::ComputePipelineHandle> dofDownsamplePipelineHandle = App.Context->CreateComputePipeline(dofDownsamplePipelineDescription);

    const EOS::ComputePipelineDescription dofBlurHPipelineDescription
    {
        .ComputeShader = dofBlurHShader,
        .DebugName     = "DOF Blur H Pipeline",
    };
    EOS::Holder<EOS::ComputePipelineHandle> dofBlurHPipelineHandle = App.Context->CreateComputePipeline(dofBlurHPipelineDescription);

    const EOS::ComputePipelineDescription dofBlurVPipelineDescription
    {
        .ComputeShader = dofBlurVShader,
        .DebugName     = "DOF Blur V Pipeline",
    };
    EOS::Holder<EOS::ComputePipelineHandle> dofBlurVPipelineHandle = App.Context->CreateComputePipeline(dofBlurVPipelineDescription);

    const EOS::ComputePipelineDescription dofCompositePipelineDescription
    {
        .ComputeShader = dofCompositeShader,
        .DebugName     = "DOF Composite Pipeline",
    };
    EOS::Holder<EOS::ComputePipelineHandle> dofCompositePipelineHandle = App.Context->CreateComputePipeline(dofCompositePipelineDescription);

    const EOS::RenderPipelineDescription presentPipelineDescription
    {
        .VertexShader     = presentVertShader,
        .FragmentShader   = presentFragShader,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat() }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Present Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> presentPipelineHandle = App.Context->CreateRenderPipeline(presentPipelineDescription);

    const FramePointers framePointers
    {
        .frameDataPtr = App.Context->GetGPUAddress(perFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(perDrawBuffer),
    };

    glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    float     lightIntensity = 1.0f;
    glm::vec3 lightColor{1.0f, 0.98f, 0.92f};
    int       debugView = 0;
    float     focusDistance = 6.0f;
    float     focusRange = 3.0f;
    float     maxBlurRadius = 6.0f;

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


        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        const EOS::TextureHandle swapChainTexture = App.Context->GetSwapChainTexture();
        App.Context->Upload(perFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);
        const float rcpW = 1.0f / static_cast<float>(App.Window.Width);
        const float rcpH = 1.0f / static_cast<float>(App.Window.Height);

        const DeferredLightingPC lightingPC
        {
            .gbufferAlbedoID   = gbufferAlbedoTexture.Index(),
            .gbufferNormalID   = gbufferNormalTexture.Index(),
            .gbufferWorldPosID = gbufferWorldPosTexture.Index(),
            .samplerID         = App.DefaultSampler.Index(),
            .outputImageID     = sceneLitTexture.Index(),
            .debugView         = static_cast<uint32_t>(debugView),
            .cameraPos         = glm::vec4(App.MainCamera.GetPosition(), 1.0f),
            .lightDirIntensity = glm::vec4(lightDirection, lightIntensity),
            .lightColorAmbient = glm::vec4(lightColor, 0.0f),
        };

        const DofDownsamplePC dofDownsamplePC
        {
            .sceneColorID  = sceneLitTexture.Index(),
            .worldPosID    = gbufferWorldPosTexture.Index(),
            .samplerID     = App.DefaultSampler.Index(),
            .outputImageID = dofHalfTexture.Index(),
            .focusDistance = focusDistance,
            .focusRange    = focusRange,
        };

        const DofBlurPC dofBlurHPC
        {
            .inputImageID  = dofHalfTexture.Index(),
            .outputImageID = dofBlurTexture.Index(),
            .samplerID     = App.DefaultSampler.Index(),
            .maxBlurRadius = maxBlurRadius,
            .texelSizeX    = 1.0f / static_cast<float>(halfWidth),
            .texelSizeY    = 1.0f / static_cast<float>(halfHeight),
        };

        const DofBlurPC dofBlurVPC
        {
            .inputImageID  = dofBlurTexture.Index(),
            .outputImageID = dofHalfTexture.Index(),
            .samplerID     = App.DefaultSampler.Index(),
            .maxBlurRadius = maxBlurRadius,
            .texelSizeX    = 1.0f / static_cast<float>(halfWidth),
            .texelSizeY    = 1.0f / static_cast<float>(halfHeight),
        };

        const DofCompositePC dofCompositePC
        {
            .sceneColorID  = sceneLitTexture.Index(),
            .blurredID     = dofHalfTexture.Index(),
            .worldPosID    = gbufferWorldPosTexture.Index(),
            .samplerID     = App.DefaultSampler.Index(),
            .outputImageID = dofTexture.Index(),
            .focusDistance = focusDistance,
            .focusRange    = focusRange,
            .maxBlurRadius = maxBlurRadius,
        };

        const PresentPC presentPC
        {
            .sceneColorID = dofTexture.Index(),
            .samplerID    = App.DefaultSampler.Index(),
        };

        constexpr EOS::RenderPass sceneRenderPass
        {
            .Color
            {
                { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 1.0f } },
                { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.5f, 0.5f, 1.0f, 1.0f } },
                { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f } }
            },
            .Depth { .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
        };



        constexpr EOS::DepthState depthState
        {
            .CompareOpState      = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { swapChainTexture,    EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { sceneLitTexture,        EOS::ResourceState::Undefined, EOS::ResourceState::ShaderResource },
            { dofHalfTexture,      EOS::ResourceState::Undefined, EOS::ResourceState::ShaderResource },
            { dofBlurTexture,      EOS::ResourceState::Undefined, EOS::ResourceState::ShaderResource },
            { dofTexture,          EOS::ResourceState::Undefined, EOS::ResourceState::ShaderResource },
            { gbufferAlbedoTexture,   EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { gbufferNormalTexture,   EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { gbufferWorldPosTexture, EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { depthTexture,           EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
        });

        // --- Pass 1: Geometry pass
        cmdPushMarker(cmdBuffer, "Geometry Pass", 0xff00f0ff);
        EOS::Framebuffer geoFramebuffer
        {
            .Color        =
            {
                { .Texture = gbufferAlbedoTexture },
                { .Texture = gbufferNormalTexture },
                { .Texture = gbufferWorldPosTexture },
            },
            .DepthStencil = { .Texture = depthTexture },
            .DebugName    = "Geometry Framebuffer",
        };
        cmdBeginRendering(cmdBuffer, sceneRenderPass, geoFramebuffer);
        {
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdPushConstants(cmdBuffer, framePointers);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, indirectDrawBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        // Transition the G-buffer textures so the lighting pass can sample them.
        cmdPipelineBarrier(cmdBuffer, {},
        {
            { gbufferAlbedoTexture,   EOS::ResourceState::RenderTarget, EOS::ResourceState::ShaderResource },
            { gbufferNormalTexture,   EOS::ResourceState::RenderTarget, EOS::ResourceState::ShaderResource },
            { gbufferWorldPosTexture, EOS::ResourceState::RenderTarget, EOS::ResourceState::ShaderResource },
            { sceneLitTexture,        EOS::ResourceState::ShaderResource, EOS::ResourceState::UnorderedAccess },
        });

        // --- Pass 2: Deferred Lighting Compute Pass
        cmdPushMarker(cmdBuffer, "Deferred Lighting Compute", 0xffffff00);
        {
            cmdBindComputePipeline(cmdBuffer, deferredLightingPipelineHandle);
            cmdPushConstants(cmdBuffer, lightingPC);
            uint32_t dispatchX = (App.Window.Width + 7) / 8;
            uint32_t dispatchY = (App.Window.Height + 7) / 8;
            cmdDispatchThreadGroups(cmdBuffer, {dispatchX, dispatchY, 1});
        }
        cmdPopMarker(cmdBuffer);

        // Transition sceneLitTexture for sampling and swapchain for rendering
        cmdPipelineBarrier(cmdBuffer, {},
        {
            { sceneLitTexture, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            { dofHalfTexture,  EOS::ResourceState::ShaderResource, EOS::ResourceState::UnorderedAccess },
        });

        // --- Pass 3: DOF Downsample
        cmdPushMarker(cmdBuffer, "DOF Downsample", 0xff00ffaa);
        {
            cmdBindComputePipeline(cmdBuffer, dofDownsamplePipelineHandle);
            cmdPushConstants(cmdBuffer, dofDownsamplePC);
            uint32_t dispatchX = (halfWidth + 7) / 8;
            uint32_t dispatchY = (halfHeight + 7) / 8;
            cmdDispatchThreadGroups(cmdBuffer, {dispatchX, dispatchY, 1});
        }
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { dofHalfTexture, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            { dofBlurTexture, EOS::ResourceState::ShaderResource, EOS::ResourceState::UnorderedAccess },
        });

        // --- Pass 4: DOF Blur H
        cmdPushMarker(cmdBuffer, "DOF Blur H", 0xff00aaff);
        {
            cmdBindComputePipeline(cmdBuffer, dofBlurHPipelineHandle);
            cmdPushConstants(cmdBuffer, dofBlurHPC);
            uint32_t dispatchX = (halfWidth + 7) / 8;
            uint32_t dispatchY = (halfHeight + 7) / 8;
            cmdDispatchThreadGroups(cmdBuffer, {dispatchX, dispatchY, 1});
        }
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { dofBlurTexture, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            { dofHalfTexture, EOS::ResourceState::ShaderResource, EOS::ResourceState::UnorderedAccess },
        });

        // --- Pass 5: DOF Blur V
        cmdPushMarker(cmdBuffer, "DOF Blur V", 0xff00bbff);
        {
            cmdBindComputePipeline(cmdBuffer, dofBlurVPipelineHandle);
            cmdPushConstants(cmdBuffer, dofBlurVPC);
            uint32_t dispatchX = (halfWidth + 7) / 8;
            uint32_t dispatchY = (halfHeight + 7) / 8;
            cmdDispatchThreadGroups(cmdBuffer, {dispatchX, dispatchY, 1});
        }
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { dofHalfTexture, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            { dofTexture,     EOS::ResourceState::ShaderResource, EOS::ResourceState::UnorderedAccess },
        });

        // --- Pass 6: DOF Composite
        cmdPushMarker(cmdBuffer, "DOF Composite", 0xff00ccff);
        {
            cmdBindComputePipeline(cmdBuffer, dofCompositePipelineHandle);
            cmdPushConstants(cmdBuffer, dofCompositePC);
            uint32_t dispatchX = (App.Window.Width + 7) / 8;
            uint32_t dispatchY = (App.Window.Height + 7) / 8;
            cmdDispatchThreadGroups(cmdBuffer, {dispatchX, dispatchY, 1});
        }
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { dofTexture, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
        });

        // --- Pass 7: Present (full-screen)
        cmdPushMarker(cmdBuffer, "Present Pass", 0xff22ff44);
        constexpr EOS::RenderPass presentRenderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.02f, 0.02f, 0.02f, 1.0f } } },
        };
        EOS::Framebuffer presentFramebuffer
        {
            .Color     = {{ .Texture = swapChainTexture }},
            .DebugName = "Present Framebuffer",
        };
        cmdBeginRendering(cmdBuffer, presentRenderPass, presentFramebuffer);
        {
            cmdBindRenderPipeline(cmdBuffer, presentPipelineHandle);
            cmdPushConstants(cmdBuffer, presentPC);
            cmdDraw(cmdBuffer, 3);
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        // --- Pass 8: UI
        App.ImGuiRenderer->BeginFrame(cmdBuffer);
        {
            ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
            ImGui::Begin("Deferred Lighting + DOF");
            ImGui::SliderFloat3("Light direction", &lightDirection.x, -1.0f, 1.0f);
            if (glm::length(lightDirection) > 0.0001f)
            {
                lightDirection = glm::normalize(lightDirection);
            }
            ImGui::SliderFloat("Light intensity", &lightIntensity, 0.0f, 2.0f);
            ImGui::ColorEdit3("Light color", &lightColor.x);
            ImGui::Separator();
            ImGui::SliderFloat("Focus distance", &focusDistance, 0.1f, 25.0f);
            ImGui::SliderFloat("Focus range", &focusRange, 0.1f, 20.0f);
            ImGui::SliderFloat("Max blur radius", &maxBlurRadius, 0.0f, 12.0f);
            constexpr const char* debugModes[] = {"Lit", "Albedo", "Normals", "Roughness", "World Position"};
            ImGui::Combo("Debug view", &debugView, debugModes, IM_ARRAYSIZE(debugModes));
            ImGui::End();
        }
        App.ImGuiRenderer->EndFrame(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{ swapChainTexture, EOS::ResourceState::RenderTarget, EOS::ResourceState::Present }});
        App.Context->Submit(cmdBuffer, swapChainTexture);
    });

    return 0;
}