
#include "../../Common/App.h"


struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;
    glm::mat4 depthMVP;
    glm::vec4 lightPos;
    glm::vec3 cameraPos;
    uint32_t  shadowMapID;
};

struct DrawData final
{
    uint32_t albedoID;
    uint32_t normalID;
    uint32_t metallicRoughnessID;
    uint32_t pad;
    glm::mat4 transform;
};

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

struct FramePointers final
{
    uint64_t frameDataPtr;
    uint64_t drawDataPtr;
};

struct Resources final
{
    EOS::ShaderModuleHolder ShaderHandleVert;
    EOS::ShaderModuleHolder ShaderHandleFrag;
    EOS::ShaderModuleHolder ShaderHandleShadowVert;
    EOS::ShaderModuleHolder ShaderHandleShadowFrag;
    EOS::Holder<EOS::TextureHandle> DepthTexture;
    EOS::Holder<EOS::TextureHandle> ShadowDepthTexture;
    EOS::SamplerHolder DepthMapSampler;
    EOS::Holder<EOS::BufferHandle> VertexBuffer;
    EOS::Holder<EOS::BufferHandle> IndexBuffer;
    EOS::Holder<EOS::BufferHandle> PerDrawBuffer;
    EOS::Holder<EOS::BufferHandle> PerFrameBuffer;
    EOS::Holder<EOS::BufferHandle> IndirectBuffer;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipelineHandle;
    EOS::Holder<EOS::RenderPipelineHandle> RenderPipelineShadowHandle;
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

    Handles.ShaderHandleVert = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleFrag = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Fragment);
    Handles.ShaderHandleShadowVert = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleShadowFrag = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Fragment);

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
        .Attributes ={{ .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) }},
        .InputBindings ={{ .Stride = sizeof(Vertex) }}
    };

    Handles.DepthTexture = App.CreateDepthTexture("Depth Buffer - ShadowMapping");

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
        .VertexShader = Handles.ShaderHandleVert,
        .FragmentShader = Handles.ShaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(Handles.DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = vertexDescriptionShadow,
        .VertexShader = Handles.ShaderHandleShadowVert,
        .FragmentShader = Handles.ShaderHandleShadowFrag,
        .DepthFormat = App.Context->GetFormat(Handles.ShadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Front,
        .DebugName = "ShadowMap Render Pipeline",
    };
    Handles.RenderPipelineShadowHandle = App.Context->CreateRenderPipeline(renderPipelineShadow);




    //TODO: Make a abstracted movement class or something, that can either behave like projection or camera projection things.
    //Light and Camera Can implement those
    const glm::mat4 m = glm::mat4(1.0f);
    glm::vec3 lightPos          = {0.0f, 20.0f, 20.0f};
    glm::vec2 lightRotation     = {-73, -90};
    const glm::mat4 lightProjection   = glm::ortho(-25.0f, 25.0f,-25.0f, 25.0f,0.1f,50.0f);
    glm::vec3 lightUp           = {0.0f, 1.0f, 0.0f};
    const FramePointers framePointers
    {
        .frameDataPtr = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
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
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMapID = Handles.ShadowDepthTexture.Index(),
        };
        App.Context->Upload(Handles.PerFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);

        EOS::Framebuffer framebufferShade
        {
            .Color = {{.Texture = App.Context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = Handles.DepthTexture },
            .DebugName = "Basic Color Depth Framebuffer",
        };

        EOS::Framebuffer framebufferShadow
        {
            .DepthStencil = { .Texture = Handles.ShadowDepthTexture },
            .DebugName = "ShadowMap framebuffer"
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


        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();

        cmdPipelineBarrier(cmdBuffer, {},
            {
                { App.Context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { Handles.DepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
                { Handles.ShadowDepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
            });

        cmdPushMarker(cmdBuffer, "Shadow Pass", 0xff0000ff);
        cmdBeginRendering(cmdBuffer, shadowRenderPass, framebufferShadow);
        {
            cmdBindVertexBuffer(cmdBuffer, 0, Handles.VertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, Handles.IndexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipelineShadowHandle);
            cmdPushConstants(cmdBuffer, framePointers);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);


        cmdPipelineBarrier(cmdBuffer, {},{{ Handles.ShadowDepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },});

        cmdPushMarker(cmdBuffer, "Shade Pass", 0xff00f0ff);
        cmdBeginRendering(cmdBuffer, renderPass, framebufferShade);
        {
            cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipelineHandle);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);


        //Render UI
        App.ImGuiRenderer->BeginFrame(cmdBuffer);
        {
            ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
            ImGui::Begin("Light Settings");

            ImGui::DragFloat3("Light Position", glm::value_ptr(lightPos));
            ImGui::DragFloat2("Light Rotation", glm::value_ptr(lightRotation));
            const uint64_t shadowArrayLayerTextureID = EOS::MakeImGuiTextureID(Handles.ShadowDepthTexture);
            ImGui::Image(shadowArrayLayerTextureID, {200,200});

            ImGui::End();
        }
        App.ImGuiRenderer->EndFrame(cmdBuffer);


        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    Handles = {};

    return 0;
}