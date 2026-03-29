
#include "../../Common/App.h"
#include "EOS.h"
#include "imgui.h"
#include "logger.h"
#include "shaders/shaderCompiler.h"
#include "utils.h"
#include "glm/gtc/type_ptr.hpp"

//https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
//https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
//http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/
//https://therealmjp.github.io/posts/shadow-maps/
//https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/

#define CASCADES 4
#define SHADOW_SIZE 4096


#pragma region DepthStates
constexpr EOS::DepthState DepthStateWrite
{
    .CompareOpState = EOS::CompareOp::Less,
    .IsDepthWriteEnabled = true,
};

constexpr EOS::DepthState DepthStateRead
{
    .CompareOpState = EOS::CompareOp::Equal,
    .IsDepthWriteEnabled = false,
};
#pragma endregion

#pragma region Structs
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
    int32_t   shadowDebugMode;
    int32_t   shadowForceCascade;
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

struct DepthReductionData final
{
    float minDepth;
    float maxDepth;
    float pad0;
    float pad1;
};

struct DepthReductionPushConstants final
{
    uint64_t depthRangePtr;
    uint32_t depthTextureID;
    uint32_t depthSamplerID;
    uint32_t width;
    uint32_t height;
};

struct CascadeSetupPushConstants final
{
    uint64_t perFramePtr;
    uint64_t depthRangePtr;
    glm::mat4 invViewProjection;
    glm::vec4 lightForwardNear;
    glm::vec4 cameraPlanes;
};

struct Cascade final
{
    float               splitDepth;
    glm::mat4           viewProjMatrix;
};
#pragma endregion

#pragma region VertexInputData
constexpr EOS::VertexInputData VertexInputDataShade
{
    .Attributes
    {
    { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) },
    { .Location = 1, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, normal) },
    { .Location = 2, .Format = EOS::VertexFormat::Float2, .Offset = offsetof(Vertex, uv) },
    { .Location = 3, .Format = EOS::VertexFormat::Float4, .Offset = offsetof(Vertex, tangent) }
    },
    .InputBindings
    {
    { .Stride = sizeof(Vertex) }
    }
};

constexpr EOS::VertexInputData VertexInputDataShadowDepth
{
    .Attributes{{ .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) }},
    .InputBindings{{ .Stride = sizeof(Vertex) }}
};
#pragma endregion

#pragma region Handles
EOS::TextureHolder DepthTexture;
EOS::TextureHolder ShadowDepthTexture;

EOS::BufferHolder VertexBuffer;
EOS::BufferHolder IndexBuffer;
EOS::BufferHolder IndirectBuffer;
EOS::BufferHolder PerDrawBuffer;
EOS::BufferHolder PerFrameBuffer;
EOS::BufferHolder DepthReductionBuffer;

EOS::RenderPipelineHolder RenderPipelineEarlyZ;
EOS::RenderPipelineHolder RenderPipelineShade;
EOS::RenderPipelineHolder RenderPipelineShadow;
EOS::ComputePipelineHolder ComputePipelineDepthReduction;
EOS::ComputePipelineHolder ComputePipelineCascadeSetup;

EOS::SamplerHolder DepthMapSampler;

EOS::ShaderModuleHolder ShaderHandleVertShade;
EOS::ShaderModuleHolder ShaderHandleFragShade;
EOS::ShaderModuleHolder ShaderHandleVertEarlyZ;
EOS::ShaderModuleHolder ShaderHandleFragEarlyZ;
EOS::ShaderModuleHolder ShaderHandleVertShadow;
EOS::ShaderModuleHolder ShaderHandleGeomShadow;
EOS::ShaderModuleHolder ShaderHandleFragShadow;
EOS::ShaderModuleHolder ShaderHandleComputeDepthReduction;
EOS::ShaderModuleHolder ShaderHandleComputeCascadeSetup;
#pragma endregion

FramePointers FramePointersData;

int nMeshes;

// Lights
glm::vec3 g_LightPos          = {0.0f, 100.0f, 20.0f};
glm::vec2 g_LightRotation     = {-73, -90};

// Cascades
std::array<Cascade, CASCADES> g_Cascades;
int g_ShadowDebugCascadeLayer = 0;
int g_ShadowDebugMode = 0;
int g_ForceShadowCascade = -1;
bool g_UseComputeCascades = false;
bool g_UseDepthReductionForCascades = true;

void CalculateCascades(const Camera& camera, float aspectRatio, const glm::vec3& lightForward, std::array<Cascade, CASCADES>& cascades);

#pragma region RenderPasses
constexpr EOS::RenderPass EarlyZRenderPass
{
    .Depth{ .LoadOpState = EOS::LoadOp::Clear }
};

constexpr EOS::RenderPass ShadowDepthRenderPass
{
    .Depth{.LoadOpState = EOS::LoadOp::Clear, .Layer = 0, .LayerCount = CASCADES}
};

constexpr EOS::RenderPass ShadeRenderPass
{
    .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
    .Depth{ .LoadOpState = EOS::LoadOp::Load}
};


void PassEarlyZ(EOS::ICommandBuffer& cmdBuffer)
{
    EOS::Framebuffer framebufferEarlyZ
    {
        .DepthStencil = { .Texture = DepthTexture },
        .DebugName = "Early-Z framebuffer",
    };

    cmdPushMarker(cmdBuffer, "Early-Z Pass", 0xff00ffff);
    cmdBeginRendering(cmdBuffer, EarlyZRenderPass, framebufferEarlyZ);
    {
        cmdBindVertexBuffer(cmdBuffer, 0, VertexBuffer);
        cmdBindIndexBuffer(cmdBuffer, IndexBuffer, EOS::IndexFormat::UI32);
        cmdBindRenderPipeline(cmdBuffer, RenderPipelineEarlyZ);
        cmdPushConstants(cmdBuffer, FramePointersData);
        cmdSetDepthState(cmdBuffer, DepthStateWrite);
        cmdDrawIndexedIndirect(cmdBuffer, IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassShadowDepth(EOS::ICommandBuffer& cmdBuffer)
{
    EOS::Framebuffer framebufferShadow
    {
        .DepthStencil = { .Texture = ShadowDepthTexture },
        .DebugName = "ShadowMap framebuffer"
    };

    cmdPushMarker(cmdBuffer, "Shadow Pass", 0xff0000ff);
    cmdBeginRendering(cmdBuffer, ShadowDepthRenderPass, framebufferShadow);
    {
        cmdBindVertexBuffer(cmdBuffer, 0, VertexBuffer);
        cmdBindIndexBuffer(cmdBuffer, IndexBuffer, EOS::IndexFormat::UI32);
        cmdBindRenderPipeline(cmdBuffer, RenderPipelineShadow);
        cmdPushConstants(cmdBuffer, FramePointersData);
        cmdSetDepthState(cmdBuffer, DepthStateWrite);
        cmdDrawIndexedIndirect(cmdBuffer, IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassShade(EOS::ICommandBuffer& cmdBuffer, const EOS::TextureHandle& swapChainTexture)
{
    EOS::Framebuffer framebufferShade
    {
        .Color = {{.Texture = swapChainTexture}},
        .DepthStencil = { .Texture = DepthTexture },
        .DebugName = "Basic Color Depth Framebuffer",
    };

    cmdPushMarker(cmdBuffer, "Shade Pass", 0xff00f0ff);
    cmdBeginRendering(cmdBuffer, ShadeRenderPass, framebufferShade);
    {
        cmdBindRenderPipeline(cmdBuffer, RenderPipelineShade);
        cmdSetDepthState(cmdBuffer, DepthStateRead);
        cmdDrawIndexedIndirect(cmdBuffer, IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassDepthReductionCompute(EOS::ICommandBuffer& cmdBuffer, const DepthReductionPushConstants& pushConstants)
{
    cmdPushMarker(cmdBuffer, "Depth Reduction Compute", 0xff22aa66);
    cmdBindComputePipeline(cmdBuffer, ComputePipelineDepthReduction);
    cmdPushConstants(cmdBuffer, pushConstants);
    cmdDispatchThreadGroups(cmdBuffer, {1, 1, 1});
    cmdPopMarker(cmdBuffer);
}

void PassCascadeSetupCompute(EOS::ICommandBuffer& cmdBuffer, const CascadeSetupPushConstants& pushConstants)
{
    cmdPushMarker(cmdBuffer, "Cascade Setup Compute", 0xff5599ff);
    cmdBindComputePipeline(cmdBuffer, ComputePipelineCascadeSetup);
    cmdPushConstants(cmdBuffer, pushConstants);
    cmdDispatchThreadGroups(cmdBuffer, {1, 1, 1});
    cmdPopMarker(cmdBuffer);
}

void PassUI(EOS::ICommandBuffer& cmdBuffer, EOS::ImGuiRenderer* UIRenderer)
{
    //Render UI
    UIRenderer->BeginFrame(cmdBuffer);
    {
        ImGui::SetNextWindowSize(ImVec2(450, 520), ImGuiCond_FirstUseEver);
        ImGui::Begin("Light Settings");

        ImGui::DragFloat3("Light Position", glm::value_ptr(g_LightPos));
        ImGui::DragFloat2("Light Rotation", glm::value_ptr(g_LightRotation));
        static const char* shadowDebugModeItems[] =
        {
            "Normal Shading",
            "Cascade Index Color",
            "Shadow UV",
            "Receiver Depth",
            "Shadow Map Depth",
            "Depth Delta Heatmap",
            "Validity Mask",
        };
        ImGui::Combo("Shadow Debug Mode", &g_ShadowDebugMode, shadowDebugModeItems, IM_ARRAYSIZE(shadowDebugModeItems));
        ImGui::SliderInt("Force Cascade", &g_ForceShadowCascade, -1, CASCADES - 1);
        if (g_ForceShadowCascade < 0)
        {
            ImGui::Text("Force Cascade: Auto");
        }
        const uint64_t shadowArrayLayerTextureID = EOS::MakeImGuiTextureID(ShadowDepthTexture, static_cast<uint32_t>(g_ShadowDebugCascadeLayer), EOS::ImGuiTextureView::Texture2DArray);
        ImGui::Image(shadowArrayLayerTextureID, {400,400});
        ImGui::SliderInt("CascadeID", &g_ShadowDebugCascadeLayer, 0, CASCADES - 1);
        ImGui::Checkbox("Use Compute Cascades", &g_UseComputeCascades);
        ImGui::Checkbox("Use Depth Reduction Range", &g_UseDepthReductionForCascades);
        ImGui::Text("Cascade Setup Path: %s", g_UseComputeCascades ? "Compute" : "CPU");
        if (g_UseComputeCascades)
        {
            ImGui::Text("Compute Range Source: %s", g_UseDepthReductionForCascades ? "Depth Reduction (min/max)" : "Camera near/far");
        }
        ImGui::End();
    }
    UIRenderer->EndFrame(cmdBuffer);
}


#pragma endregion

void DestroyHandles()
{
    DepthTexture.Reset();
    ShadowDepthTexture.Reset();
    VertexBuffer.Reset();
    IndexBuffer.Reset();
    IndirectBuffer.Reset();
    PerDrawBuffer.Reset();
    PerFrameBuffer.Reset();
    DepthReductionBuffer.Reset();
    RenderPipelineEarlyZ.Reset();
    RenderPipelineShade.Reset();
    RenderPipelineShadow.Reset();
    ComputePipelineDepthReduction.Reset();
    ComputePipelineCascadeSetup.Reset();
    DepthMapSampler.Reset();
    ShaderHandleVertShade.Reset();
    ShaderHandleFragShade.Reset();
    ShaderHandleVertEarlyZ.Reset();
    ShaderHandleFragEarlyZ.Reset();
    ShaderHandleVertShadow.Reset();
    ShaderHandleGeomShadow.Reset();
    ShaderHandleFragShadow.Reset();
    ShaderHandleComputeDepthReduction.Reset();
    ShaderHandleComputeCascadeSetup.Reset();
}

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

    ShaderHandleVertShade = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Vertex);
    ShaderHandleFragShade = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Fragment);
    ShaderHandleVertEarlyZ = App.Context->CreateShaderModule("earlyZ", EOS::ShaderStage::Vertex);
    ShaderHandleFragEarlyZ = App.Context->CreateShaderModule("earlyZ", EOS::ShaderStage::Fragment);
    ShaderHandleVertShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Vertex);
    ShaderHandleGeomShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Geometry);
    ShaderHandleFragShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Fragment);
    ShaderHandleComputeDepthReduction = App.Context->CreateShaderModule("depthReduction", EOS::ShaderStage::Compute);
    ShaderHandleComputeCascadeSetup = App.Context->CreateShaderModule("cascadeSetup", EOS::ShaderStage::Compute);

    DepthTexture = App.Context->CreateTexture(
    {
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height)},
        .Usage                  = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName              = "Depth Buffer",
    });

    constexpr EOS::Dimensions shadowMapSize{SHADOW_SIZE, SHADOW_SIZE};
    constexpr EOS::TextureDescription shadowMapDescription
    {
        .Type = EOS::ImageType::Image_2D_Array,
        .TextureFormat = EOS::Format::Z_F32,
        .TextureDimensions = shadowMapSize,
        .NumberOfLayers = CASCADES,
        .Usage = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName = "Shadow Depth Buffer"
    };
    ShadowDepthTexture = App.Context->CreateTexture(shadowMapDescription);

    constexpr EOS::SamplerDescription depthMapSamplerDesc
    {
        .wrapU = EOS::SamplerWrap::ClampToBorder,
        .wrapV = EOS::SamplerWrap::ClampToBorder,
        .wrapW = EOS::SamplerWrap::ClampToBorder,
        .maxAnisotropic = 0,
        .depthCompareEnabled = false,
        .debugName = "DepthMap Sampler",
    };
    DepthMapSampler = App.Context->CreateSampler(depthMapSamplerDesc);


    //Scene scene = LoadModel("../data/ABeautifulGame/ABeautifulGame.gltf", App.Context.get());
    //const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(10.00f));

    Scene scene = LoadModel("../data/sponza/Sponza.gltf", App.Context.get());
    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
    std::vector<Vertex> vertices;
    vertices.reserve(scene.vertices.size());
    for (const VertexInformation& vertexInfo : scene.vertices)  vertices.emplace_back(vertexInfo.position, vertexInfo.normal, vertexInfo.uv, vertexInfo.tangent);

    VertexBuffer = App.Context->CreateBuffer(
    {
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });

    IndexBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::Index,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(uint32_t) * scene.indices.size(),
        .Data      = scene.indices.data(),
        .DebugName = "Buffer: index"
    });

    std::vector<DrawData> drawData;
    nMeshes = scene.meshes.size();
    drawData.reserve(nMeshes);
    for (auto& mesh : scene.meshes)
    {
        drawData.push_back(
    {
            .albedoID            = mesh.albedoTextureIdx,
            .normalID            = mesh.normalTextureIdx,
            .metallicRoughnessID = mesh.metallicRoughnessTextureIdx,
            .transform           = mesh.transform,
        });
    }

    PerDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "perDrawBuffer",
    });

    PerFrameBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "perFrameBuffer",
    });

    constexpr DepthReductionData initialDepthReductionData
    {
        .minDepth = 1.0f,
        .maxDepth = 0.0f,
    };

    DepthReductionBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DepthReductionData),
        .Data      = &initialDepthReductionData,
        .DebugName = "DepthReductionBuffer",
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

    IndirectBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectBuffer",
    });

    EOS::RenderPipelineDescription renderPipelineShade
    {
        .VertexInput = VertexInputDataShade,
        .VertexShader = ShaderHandleVertShade,
        .FragmentShader = ShaderHandleFragShade,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    RenderPipelineShade = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineEarlyZDesc
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = ShaderHandleVertEarlyZ,
        .FragmentShader = ShaderHandleFragEarlyZ,
        .DepthFormat = App.Context->GetFormat(DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "EarlyZ Render Pipeline",
    };
    RenderPipelineEarlyZ = App.Context->CreateRenderPipeline(renderPipelineEarlyZDesc);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = ShaderHandleVertShadow,
        .GeometryShader = ShaderHandleGeomShadow,
        .FragmentShader = ShaderHandleFragShadow,
        .DepthFormat = App.Context->GetFormat(ShadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DepthClamping = true,
        .DebugName = "ShadowMap Render Pipeline",
    };
    RenderPipelineShadow = App.Context->CreateRenderPipeline(renderPipelineShadow);

    const EOS::ComputePipelineDescription computeDepthReductionDesc
    {
        .ComputeShader = ShaderHandleComputeDepthReduction,
        .DebugName = "Depth Reduction Compute Pipeline",
    };
    ComputePipelineDepthReduction = App.Context->CreateComputePipeline(computeDepthReductionDesc);

    const EOS::ComputePipelineDescription computeCascadeSetupDesc
    {
        .ComputeShader = ShaderHandleComputeCascadeSetup,
        .DebugName = "Cascade Setup Compute Pipeline",
    };
    ComputePipelineCascadeSetup = App.Context->CreateComputePipeline(computeCascadeSetupDesc);

    //Get UBO pointers
    FramePointersData =
    {
        .frameDataPtr = App.Context->GetGPUAddress(PerFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(PerDrawBuffer),
    };


    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);
        if (std::isnan(aspectRatio)) return;

        //Update Light Frustum
        glm::vec3 lightForward;
        lightForward.x = cos(glm::radians(g_LightRotation.y)) * cos(glm::radians(g_LightRotation.x));
        lightForward.y = sin(glm::radians(g_LightRotation.x));
        lightForward.z = sin(glm::radians(g_LightRotation.y)) * cos(glm::radians(g_LightRotation.x));
        lightForward = glm::normalize(lightForward);

        //Get camera matrices
        const glm::mat4 view = App.MainCamera.GetViewMatrix();
        const glm::mat4 projection = App.MainCamera.GetProjectionMatrix(aspectRatio);
        const glm::mat4 viewProjection = projection * view;
        const glm::mat4 mvp = viewProjection * m;

        PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .view = view,
            .lightPos = glm::vec4(g_LightPos, 1.0f),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMapID = ShadowDepthTexture.Index(),
            .shadowDebugMode = g_ShadowDebugMode,
            .shadowForceCascade = g_ForceShadowCascade,
        };

        if (!g_UseComputeCascades)
        {
            CalculateCascades(App.MainCamera, aspectRatio, lightForward, g_Cascades);

            for (uint32_t cascadeID = 0; cascadeID < CASCADES; ++cascadeID)
            {
                perFrameData.cascadeViewProj[cascadeID] = g_Cascades[cascadeID].viewProjMatrix;
            }
            perFrameData.cascadeSplits = glm::vec4(
                g_Cascades[0].splitDepth,
                g_Cascades[1].splitDepth,
                g_Cascades[2].splitDepth,
                g_Cascades[3].splitDepth
            );
        }

        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        const EOS::TextureHandle swapChainTexture = App.Context->GetSwapChainTexture();
        App.Context->Upload(PerFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);

        cmdPipelineBarrier(cmdBuffer, {},
{
                { swapChainTexture, EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { DepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
                { ShadowDepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
            });


        PassEarlyZ(cmdBuffer);

        if (g_UseComputeCascades)
        {
            const DepthReductionPushConstants depthReductionPC
            {
                .depthRangePtr = App.Context->GetGPUAddress(DepthReductionBuffer),
                .depthTextureID = DepthTexture.Index(),
                .depthSamplerID = DepthMapSampler.Index(),
                .width = static_cast<uint32_t>(App.Window.Width),
                .height = static_cast<uint32_t>(App.Window.Height),
            };

            const CascadeSetupPushConstants cascadeSetupPC
            {
                .perFramePtr = App.Context->GetGPUAddress(PerFrameBuffer),
                .depthRangePtr = App.Context->GetGPUAddress(DepthReductionBuffer),
                .invViewProjection = glm::inverse(viewProjection),
                .lightForwardNear = glm::vec4(lightForward, 0.0f),
                .cameraPlanes = glm::vec4(App.MainCamera.GetNearPlane(), App.MainCamera.GetFarPlane(), g_UseDepthReductionForCascades ? 1.0f : 0.0f, static_cast<float>(SHADOW_SIZE)),
            };

            cmdPipelineBarrier(cmdBuffer,
            {
                { DepthReductionBuffer, EOS::ResourceState::Undefined, EOS::ResourceState::UnorderedAccess },
            },
            {
                { DepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },
            });

            PassDepthReductionCompute(cmdBuffer, depthReductionPC);

            cmdPipelineBarrier(cmdBuffer,
            {
                { DepthReductionBuffer, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
                { PerFrameBuffer, EOS::ResourceState::Undefined, EOS::ResourceState::UnorderedAccess },
            },
            {});

            PassCascadeSetupCompute(cmdBuffer, cascadeSetupPC);

            cmdPipelineBarrier(cmdBuffer,
            {
                { PerFrameBuffer, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            },
            {
                { DepthTexture, EOS::ResourceState::ShaderResource, EOS::ResourceState::DepthRead },
            });
        }
        else
        {
            cmdPipelineBarrier(cmdBuffer, {},
            {
                { DepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::DepthRead },
            });
        }

        PassShadowDepth(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {},
        {
            { ShadowDepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },
            { DepthTexture, EOS::ResourceState::DepthRead, EOS::ResourceState::DepthWrite },
        });

        PassShade(cmdBuffer, swapChainTexture);
        PassUI(cmdBuffer, App.ImGuiRenderer.get());

        cmdPipelineBarrier(cmdBuffer, {}, {{swapChainTexture, EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});

        App.Context->Submit(cmdBuffer, swapChainTexture);
    });

    scene.Cleanup();
    DestroyHandles();
    return 0;
}

void CalculateCascades(const Camera& camera, float aspectRatio, const glm::vec3& lightForward, std::array<Cascade, CASCADES>& cascades)
{
    //Calculate Cascades
    float cascadeSplits[CASCADES];

    const float nearClip = camera.GetNearPlane();
    const float farClip = camera.GetFarPlane();
    const float clipRange = farClip - nearClip;

    const float minZ = nearClip;
    const float maxZ = nearClip + clipRange;

    const float range = maxZ - minZ;
    const float ratio = maxZ / minZ;

    //Calculate split distances based on article in GPU Gems 3
    //Uses logarithmic and uniform split scheme
    for (uint32_t i = 0; i < CASCADES; ++i)
    {
        constexpr float cascadeLambda = 0.25f;
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

        //Get NDC Coordinates
        //Vulkan Depth is [0 - 1]
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

        //Project frustum corners into world space
        glm::mat4 invCam = glm::inverse(camera.GetViewProjectionMatrix(aspectRatio));
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

        //Find the longest radius of the frustum
        float radius = 0.0f;
        for (uint32_t j = 0; j < 8; j++)
        {
            float distance = glm::length(frustumCorners[j] - frustumCenter);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        //Create light ortho based on the AABB for this cascade
        glm::vec3 maxExtents = glm::vec3(radius);
        glm::vec3 minExtents = -maxExtents;
        glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightForward * -minExtents.z, frustumCenter, lightUp);
        glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

        //Texel Grid Stabilization
        //Avoid light shimmering -> Create rounding matrix so we move in texel sized increments
        constexpr float shadowMapResolution = 4096.0f;
        const glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
        glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin = shadowOrigin * (shadowMapResolution * 0.5f);
        const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
        glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / shadowMapResolution);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightOrthoMatrix[3] += roundOffset;

        // Store split distance and matrix in cascade
        cascades[i].splitDepth = nearClip + splitDist * clipRange;
        cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;

        lastSplitDist = cascadeSplits[i];
    }
}