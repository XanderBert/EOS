
#include "../../Common/App.h"
#include "EOS.h"
#include "imgui.h"
#include "logger.h"
#include "shaders/shaderCompiler.h"
#include "utils.h"
#include "glm/gtc/type_ptr.hpp"

#define CASCADES 4
struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;
    glm::mat4 view;
    glm::mat4 cascadeViewProj[CASCADES];
    glm::vec4 lightPos;
    glm::vec4 cascadeSplits;
    glm::vec3 cameraPos;
    uint32_t  shadowMapID;
    bool      showCascades;
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


struct VertexShadow final
{
    glm::vec3 position;
};

struct FramePointers final
{
    uint64_t frameDataPtr;
    uint64_t drawDataPtr;
    uint32_t cascadeID;
    uint32_t _pad;
};

struct Cascade final
{
    EOS::Framebuffer    frameBuffer;
    float               splitDepth;
    glm::mat4           viewProjMatrix;
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
        .origin = {0.0f, 10.0f, -3.0f},
        .rotation = {0, 0.0f},
        .acceleration = 100.0f
    };

    ExampleAppDescription appDescription
    {
        .contextDescription = contextDescr,
        .cameraDescription = cameraDescription
    };

    ExampleApp App{appDescription};

    EOS::ShaderModuleHolder shaderHandleVert = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder shaderHandleFrag = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Fragment);
    EOS::ShaderModuleHolder shaderHandleShadowVert = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Vertex);
    EOS::ShaderModuleHolder shaderHandleShadowFrag = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Fragment);

    constexpr EOS::RenderPass renderPass
    {
        .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
        .Depth{ .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
    };

    constexpr EOS::RenderPass shadowRenderPass
    {
        .Depth{.LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
    };

    constexpr EOS::DepthState depthState
    {
        .CompareOpState = EOS::CompareOp::Less,
        .IsDepthWriteEnabled = true,
    };

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

    EOS::Holder<EOS::TextureHandle> depthTexture = App.CreateDepthTexture();

    constexpr EOS::Dimensions shadowMapSize{4096, 4096};
    constexpr EOS::TextureDescription shadowMapDescription
    {
        .Type = EOS::ImageType::Image_2D_Array,
        .TextureFormat = EOS::Format::Z_F32,
        .TextureDimensions = shadowMapSize,
        .NumberOfLayers = CASCADES,
        .Usage = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName = "Shadow Depth Buffer"
    };
    EOS::Holder<EOS::TextureHandle> shadowDepthTexture = App.Context->CreateTexture(shadowMapDescription);


    constexpr EOS::SamplerDescription depthMapSamplerDesc
    {
        .wrapU = EOS::SamplerWrap::ClampToBorder,
        .wrapV = EOS::SamplerWrap::ClampToBorder,
        .wrapW = EOS::SamplerWrap::ClampToBorder,
        .maxAnisotropic = 0,
        .depthCompareEnabled = false,
        .debugName = "DepthMap Sampler",
    };
    EOS::SamplerHolder depthMapSampler = App.Context->CreateSampler(depthMapSamplerDesc);


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

    EOS::Holder<EOS::BufferHandle> perDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "perDrawBuffer",
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
        .VertexInput = vertexDesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(depthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = vertexDescriptionShadow,
        .VertexShader = shaderHandleShadowVert,
        .FragmentShader = shaderHandleShadowFrag,
        .DepthFormat = App.Context->GetFormat(shadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "ShadowMap Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineShadowHandle = App.Context->CreateRenderPipeline(renderPipelineShadow);


    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(0.04f));
    glm::vec3 lightPos          = {0.0f, 100.0f, 20.0f};
    glm::vec2 lightRotation     = {-73, -90};

    FramePointers framePointers
    {
        .frameDataPtr = App.Context->GetGPUAddress(perFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(perDrawBuffer),
    };

    std::array<Cascade, CASCADES> cascades;
    int shadowDebugCascadeLayer = 0;
    bool showCascades = false;

    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);
        if (std::isnan(aspectRatio)) return;

        //Update Light Frustum
        glm::vec3 lightForward;
        lightForward.x = cos(glm::radians(lightRotation.y)) * cos(glm::radians(lightRotation.x));
        lightForward.y = sin(glm::radians(lightRotation.x));
        lightForward.z = sin(glm::radians(lightRotation.y)) * cos(glm::radians(lightRotation.x));
        lightForward = glm::normalize(lightForward);

        //Get camera matrices
        const glm::mat4 view = App.MainCamera.GetViewMatrix();
        const glm::mat4 projection = App.MainCamera.GetProjectionMatrix(aspectRatio);
        const glm::mat4 viewProjection = projection * view;
        const glm::mat4 mvp = viewProjection * m;

        //Calculate Cascades
        float cascadeSplits[CASCADES];

        float nearClip = App.MainCamera.GetNearPlane();
        float farClip = App.MainCamera.GetFarPlane();
        float clipRange = farClip - nearClip;

        float minZ = nearClip;
        float maxZ = nearClip + clipRange;

        float range = maxZ - minZ;
        float ratio = maxZ / minZ;


        //https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        for (uint32_t i = 0; i < CASCADES; ++i)
        {
            constexpr float cascadeLambda = 0.5f;
            float p = (i + 1) / static_cast<float>(CASCADES);
            float log = minZ * std::pow(ratio, p);
            float uniform = minZ + range * p;
            float d = cascadeLambda * (log - uniform) + uniform;
            cascadeSplits[i] = (d - nearClip) / clipRange;
        }


        float lastSplitDist = 0.0;
        for (uint32_t i = 0; i < CASCADES; ++i)
        {
            float splitDist = cascadeSplits[i];

            glm::vec3 frustumCorners[8] =
            {
                glm::vec3(-1.0f,  1.0f, 0.0f),
                glm::vec3( 1.0f,  1.0f, 0.0f),
                glm::vec3( 1.0f, -1.0f, 0.0f),
                glm::vec3(-1.0f, -1.0f, 0.0f),
                glm::vec3(-1.0f,  1.0f,  1.0f),
                glm::vec3( 1.0f,  1.0f,  1.0f),
                glm::vec3( 1.0f, -1.0f,  1.0f),
                glm::vec3(-1.0f, -1.0f,  1.0f),
            };

            // Project frustum corners into world space
            glm::mat4 invCam = glm::inverse(viewProjection);
            for (uint32_t j = 0; j < 8; ++j)
            {
                glm::vec4 invCorner = invCam * glm::vec4(frustumCorners[j], 1.0f) ;
                frustumCorners[j] = invCorner / invCorner.w;
            }

            for (uint32_t j = 0; j < 4; ++j)
            {
                glm::vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
                frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
                frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
            }

            // Get frustum center
            glm::vec3 frustumCenter = glm::vec3(0.0f);
            for (uint32_t j = 0; j < 8; j++)
            {
                frustumCenter += frustumCorners[j];
            }
            frustumCenter /= 8.0f;

            float radius = 0.0f;
            for (uint32_t j = 0; j < 8; j++)
            {
                float distance = glm::length(frustumCorners[j] - frustumCenter);
                radius = glm::max(radius, distance);
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;
            glm::vec3 maxExtents = glm::vec3(radius);
            glm::vec3 minExtents = -maxExtents;
            glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);

            glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightForward * -minExtents.z, frustumCenter, lightUp);
            glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

            // Store split distance and matrix in cascade
            cascades[i].splitDepth = App.MainCamera.GetNearPlane() + splitDist * clipRange;
            cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;

            lastSplitDist = cascadeSplits[i];
        }


        PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .view = view,
            .lightPos = glm::vec4(lightPos, 1.0f),
            .cascadeSplits = glm::vec4(cascades[0].splitDepth, cascades[1].splitDepth, cascades[2].splitDepth, cascades[3].splitDepth),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMapID = shadowDepthTexture.Index(),
            .showCascades = showCascades,
        };
        for (uint32_t i = 0; i < CASCADES; ++i)
        {
            perFrameData.cascadeViewProj[i] = cascades[i].viewProjMatrix;
        }
        App.Context->Upload(perFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);

        EOS::Framebuffer framebufferShade
        {
            .Color = {{.Texture = App.Context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = depthTexture },
            .DebugName = "Basic Color Depth Framebuffer",
        };


        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();

        cmdPipelineBarrier(cmdBuffer, {},
            {
                { App.Context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { depthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
                { shadowDepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
            });

        cmdPushMarker(cmdBuffer, "Shadow Pass", 0xff0000ff);
        for (uint32_t cascadeLayer = 0; cascadeLayer < CASCADES; ++cascadeLayer)
        {
            EOS::RenderPass shadowRenderPassForCascade = shadowRenderPass;
            shadowRenderPassForCascade.Depth.Layer = static_cast<uint8_t>(cascadeLayer);

            EOS::Framebuffer framebufferShadow
            {
                .DepthStencil = { .Texture = shadowDepthTexture },
                .DebugName = "ShadowMap framebuffer"
            };

            cmdBeginRendering(cmdBuffer, shadowRenderPassForCascade, framebufferShadow);
            {
                cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
                cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
                cmdBindRenderPipeline(cmdBuffer, renderPipelineShadowHandle);
                framePointers.cascadeID = cascadeLayer;
                cmdPushConstants(cmdBuffer, framePointers);
                cmdSetDepthState(cmdBuffer, depthState);
                cmdDrawIndexedIndirect(cmdBuffer, indirectBuffer, 0, scene.meshes.size());
            }
            cmdEndRendering(cmdBuffer);
        }
        cmdPopMarker(cmdBuffer);

        framePointers.cascadeID = 0;

        cmdPipelineBarrier(cmdBuffer, {},{{ shadowDepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },});

        cmdPushMarker(cmdBuffer, "Shade Pass", 0xff00f0ff);
        cmdBeginRendering(cmdBuffer, renderPass, framebufferShade);
        {
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexedIndirect(cmdBuffer, indirectBuffer, 0, scene.meshes.size());
        }
        cmdEndRendering(cmdBuffer);
        cmdPopMarker(cmdBuffer);


        //Render UI
        App.ImGuiRenderer->BeginFrame(cmdBuffer);
        {
            ImGui::SetNextWindowSize(ImVec2(450, 520), ImGuiCond_FirstUseEver);
            ImGui::Begin("Light Settings");

            ImGui::DragFloat3("Light Position", glm::value_ptr(lightPos));
            ImGui::DragFloat2("Light Rotation", glm::value_ptr(lightRotation));
            const uint64_t shadowArrayLayerTextureID = EOS::MakeImGuiTextureID(shadowDepthTexture, static_cast<uint32_t>(shadowDebugCascadeLayer), EOS::ImGuiTextureView::Texture2DArray);
            ImGui::Image(shadowArrayLayerTextureID, {400,400});
            ImGui::SliderInt("CascadeID", &shadowDebugCascadeLayer, 0, CASCADES - 1);
            ImGui::Checkbox("Show Cascades", &showCascades);
            ImGui::End();
        }
        App.ImGuiRenderer->EndFrame(cmdBuffer);


        cmdPipelineBarrier(cmdBuffer, {}, {{App.Context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        App.Context->Submit(cmdBuffer, App.Context->GetSwapChainTexture());
    });

    return 0;
}