#include "../../Common/App.h"
#include "EOS.h"
#include "imgui.h"
#include "logger.h"
#include "shaders/shaderCompiler.h"
#include "utils.h"

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
    uint32_t debugView;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
    glm::vec4 cameraPos;
    glm::vec4 lightDirIntensity;
    glm::vec4 lightColorAmbient;
};

struct EdgeDetectPC final
{
    uint32_t sceneColorID;
    uint32_t sceneNormalID;
    uint32_t samplerID;
    float    threshold;
    uint32_t showEdgesOnly;
    float    texelW;
    float    texelH;
    uint32_t pad0;
};

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

    EOS::ShaderModuleHolder vertexShader = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder pixelShader  = App.Context->CreateShaderModule("indirectModel", EOS::ShaderStage::Fragment);
    EOS::ShaderModuleHolder deferredLightVertShader = App.Context->CreateShaderModule("deferredLight", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder deferredLightFragShader = App.Context->CreateShaderModule("deferredLight", EOS::ShaderStage::Fragment);
    EOS::ShaderModuleHolder edgeDetectVertShader = App.Context->CreateShaderModule("edgeDetect", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder edgeDetectFragShader = App.Context->CreateShaderModule("edgeDetect", EOS::ShaderStage::Fragment);
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
        .Usage            = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName        = "Deferred Lit Scene",
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

    const EOS::RenderPipelineDescription deferredLightingPipelineDescription
    {
        .VertexShader     = deferredLightVertShader,
        .FragmentShader   = deferredLightFragShader,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetFormat(sceneLitTexture) }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Deferred Lighting Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> deferredLightingPipelineHandle = App.Context->CreateRenderPipeline(deferredLightingPipelineDescription);

    const EOS::RenderPipelineDescription edgePipelineDescription
    {
        .VertexShader     = edgeDetectVertShader,
        .FragmentShader   = edgeDetectFragShader,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat() }},
        .PipelineCullMode = EOS::CullMode::None,
        .DebugName        = "Edge Detect Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> edgePipelineHandle = App.Context->CreateRenderPipeline(edgePipelineDescription);

    const FramePointers framePointers
    {
        .frameDataPtr = App.Context->GetGPUAddress(perFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(perDrawBuffer),
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
            .debugView         = static_cast<uint32_t>(debugView),
            .cameraPos         = glm::vec4(App.MainCamera.GetPosition(), 1.0f),
            .lightDirIntensity = glm::vec4(lightDirection, lightIntensity),
            .lightColorAmbient = glm::vec4(lightColor, 0.0f),
        };

        const EdgeDetectPC edgePC
        {
            .sceneColorID   = sceneLitTexture.Index(),
            .sceneNormalID  = gbufferNormalTexture.Index(),
            .samplerID      = App.DefaultSampler.Index(),
            .threshold      = edgeThreshold,
            .showEdgesOnly  = showEdgesOnly ? 1u : 0u,
            .texelW         = rcpW,
            .texelH         = rcpH,
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

        constexpr EOS::RenderPass lightingRenderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear } },
        };

        constexpr EOS::RenderPass edgeRenderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear } },
        };

        constexpr EOS::DepthState depthState
        {
            .CompareOpState      = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { swapChainTexture,    EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { sceneLitTexture,     EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { gbufferAlbedoTexture,   EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { gbufferNormalTexture,   EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { gbufferWorldPosTexture, EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
            { depthTexture,        EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite   },
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
        });

        // --- Pass 2: Lighting pass
        cmdPushMarker(cmdBuffer, "Deferred Lighting Pass", 0xffffff00);
        EOS::Framebuffer lightingFramebuffer
        {
            .Color     = {{ .Texture = sceneLitTexture }},
            .DebugName = "Deferred Lighting Framebuffer",
        };
        cmdBeginRendering(cmdBuffer, lightingRenderPass, lightingFramebuffer);
        {
            cmdBindRenderPipeline(cmdBuffer, deferredLightingPipelineHandle);
            cmdPushConstants(cmdBuffer, lightingPC);
            cmdDraw(cmdBuffer, 3);
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { sceneLitTexture, EOS::ResourceState::RenderTarget, EOS::ResourceState::ShaderResource },
        });

        // --- Pass 3: Edge detection pass
        cmdPushMarker(cmdBuffer, "Edge Detect Pass", 0xff22ff44);
        EOS::Framebuffer edgeFramebuffer
        {
            .Color     = {{ .Texture = swapChainTexture }},
            .DebugName = "Edge Detect Framebuffer",
        };
        cmdBeginRendering(cmdBuffer, edgeRenderPass, edgeFramebuffer);
        {
            cmdBindRenderPipeline(cmdBuffer, edgePipelineHandle);
            cmdPushConstants(cmdBuffer, edgePC);
            cmdDraw(cmdBuffer, 3);
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);

        // --- Pass 3: UI
        App.ImGuiRenderer->BeginFrame(cmdBuffer);
        {
            ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
            ImGui::Begin("Deferred Edge Detection");
            ImGui::SliderFloat3("Light direction", &lightDirection.x, -1.0f, 1.0f);
            if (glm::length(lightDirection) > 0.0001f)
            {
                lightDirection = glm::normalize(lightDirection);
            }
            ImGui::SliderFloat("Light intensity", &lightIntensity, 0.0f, 2.0f);
            ImGui::ColorEdit3("Light color", &lightColor.x);
            ImGui::Separator();
            ImGui::SliderFloat("Edge threshold", &edgeThreshold, 2.0f, 8.0f);
            ImGui::Checkbox("Edges only", &showEdgesOnly);
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