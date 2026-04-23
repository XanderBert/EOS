
#include "../../Common/App.h"

//https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
//https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
//http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/
//https://therealmjp.github.io/posts/shadow-maps/
//https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
//https://www.researchgate.net/publication/220791941_Sample_distribution_Shadow_Maps
//https://advances.realtimerendering.com/s2010/Lauritzen-SDSM(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf

#define CASCADES 4
#define SHADOW_SIZE 4096

enum ShadowTechnique : int32_t
{
    ShadowTechniqueCpuCascade = 0,
    ShadowTechniqueComputeCascade = 1,
    ShadowTechniqueRayQuery = 2,
};

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
    glm::vec4 lightDir;
    glm::vec4 cascadeSplits;
    glm::vec3 cameraPos;
    uint32_t  shadowMapID;
    uint32_t  sceneTLASID;
    int32_t   shadowDebugMode;
    int32_t   shadowForceCascade;
    int32_t   shadowTechnique;
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
    uint32_t width;
    uint32_t height;
    uint32_t numGroupsX;
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
struct Resources final
{
    EOS::TextureHolder DepthTexture;
    EOS::TextureHolder ShadowDepthTexture;

    EOS::BufferHolder VertexBuffer;
    EOS::BufferHolder IndexBuffer;
    EOS::BufferHolder IndirectBuffer;
    EOS::BufferHolder PerDrawBuffer;
    EOS::BufferHolder PerFrameBuffer;
    EOS::BufferHolder DepthReductionBuffer;
    EOS::BufferHolder AccelVertexBuffer;
    EOS::BufferHolder AccelIndexBuffer;
    EOS::BufferHolder AccelTransformBuffer;
    EOS::BufferHolder AccelInstancesBuffer;

    std::vector<EOS::AccelStructHolder> SceneBLASes;
    EOS::AccelStructHolder SceneTLAS;

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
};

Resources Handles;
#pragma endregion

FramePointers FramePointersData;

int32_t nMeshes;

// Lights
glm::vec2 g_LightRotation     = {-73, -90};

// Cascades
std::array<Cascade, CASCADES> g_Cascades;
int g_ShadowDebugCascadeLayer = 0;
int g_ShadowDebugMode = 0;
int g_ForceShadowCascade = -1;
bool g_UseDepthReductionForCascades = true;

int g_ShadowTechnique = ShadowTechniqueCpuCascade;

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
        float p = (static_cast<float>(i) + 1.0f) / static_cast<float>(CASCADES);
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
        for (auto & frustumCorner : frustumCorners)
        {
            glm::vec4 invCorner = invCam * glm::vec4(frustumCorner, 1.0f) ;
            frustumCorner = invCorner / invCorner.w;
        }

        for (uint32_t j = 0; j < 4; ++j)
        {
            glm::vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
            frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
        }

        // Get frustum center
        auto frustumCenter = glm::vec3(0.0f);
        for (auto frustumCorner : frustumCorners)
        {
            frustumCenter += frustumCorner;
        }
        frustumCenter /= 8.0f;

        //Find the longest radius of the frustum
        float radius = 0.0f;
        for (auto frustumCorner : frustumCorners)
        {
            float distance = glm::length(frustumCorner - frustumCenter);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        //Create light ortho based on the AABB for this cascade
        glm::vec3 maxExtents = glm::vec3(radius);
        glm::vec3 minExtents = -maxExtents;
        constexpr glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightForward * -minExtents.z, frustumCenter, lightUp);
        glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

        //Texel Grid Stabilization
        //Avoid light shimmering -> Create rounding matrix so we move in texel sized increments
        const glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
        glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin = shadowOrigin * (SHADOW_SIZE * 0.5f);
        const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
        glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / SHADOW_SIZE);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightOrthoMatrix[3] += roundOffset;

        // Store split distance and matrix in cascade
        cascades[i].splitDepth = nearClip + splitDist * clipRange;
        cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;

        lastSplitDist = cascadeSplits[i];
    }
}
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
        .DepthStencil = { .Texture = Handles.DepthTexture },
        .DebugName = "Early-Z framebuffer",
    };

    cmdPushMarker(cmdBuffer, "Early-Z Pass", 0xff00ffff);
    cmdBeginRendering(cmdBuffer, EarlyZRenderPass, framebufferEarlyZ);
    {
        cmdBindVertexBuffer(cmdBuffer, 0, Handles.VertexBuffer);
        cmdBindIndexBuffer(cmdBuffer, Handles.IndexBuffer, EOS::IndexFormat::UI32);
        cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipelineEarlyZ);
        cmdPushConstants(cmdBuffer, FramePointersData);
        cmdSetDepthState(cmdBuffer, DepthStateWrite);
        cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassShadowDepth(EOS::ICommandBuffer& cmdBuffer)
{
    EOS::Framebuffer framebufferShadow
    {
        .DepthStencil = { .Texture = Handles.ShadowDepthTexture },
        .DebugName = "ShadowMap framebuffer"
    };

    cmdPushMarker(cmdBuffer, "Shadow Pass", 0xff0000ff);
    cmdBeginRendering(cmdBuffer, ShadowDepthRenderPass, framebufferShadow);
    {
        cmdBindVertexBuffer(cmdBuffer, 0, Handles.VertexBuffer);
        cmdBindIndexBuffer(cmdBuffer, Handles.IndexBuffer, EOS::IndexFormat::UI32);
        cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipelineShadow);
        cmdPushConstants(cmdBuffer, FramePointersData);
        cmdSetDepthState(cmdBuffer, DepthStateWrite);
        cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassShade(EOS::ICommandBuffer& cmdBuffer, const EOS::TextureHandle& swapChainTexture)
{
    EOS::Framebuffer framebufferShade
    {
        .Color = {{.Texture = swapChainTexture}},
        .DepthStencil = { .Texture = Handles.DepthTexture },
        .DebugName = "Basic Color Depth Framebuffer",
    };

    cmdPushMarker(cmdBuffer, "Shade Pass", 0xff00f0ff);
    cmdBeginRendering(cmdBuffer, ShadeRenderPass, framebufferShade);
    {
        cmdBindRenderPipeline(cmdBuffer, Handles.RenderPipelineShade);
        cmdSetDepthState(cmdBuffer, DepthStateRead);
        cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, nMeshes);
    }
    cmdEndRendering(cmdBuffer);
    cmdPopMarker(cmdBuffer);
}

void PassDepthReductionCompute(EOS::ICommandBuffer& cmdBuffer, const DepthReductionPushConstants& pc)
{
    const uint32_t gy = (pc.height + 15) / 16;

    cmdPushMarker(cmdBuffer, "Depth Reduction Compute", 0xff22aa66);
    cmdBindComputePipeline(cmdBuffer, Handles.ComputePipelineDepthReduction);
    cmdPushConstants(cmdBuffer, pc);
    cmdDispatchThreadGroups(cmdBuffer, {pc.numGroupsX, gy, 1});
    cmdPopMarker(cmdBuffer);
}

void PassCascadeSetupCompute(EOS::ICommandBuffer& cmdBuffer, const CascadeSetupPushConstants& pushConstants)
{
    cmdPushMarker(cmdBuffer, "Cascade Setup Compute", 0xff5599ff);
    cmdBindComputePipeline(cmdBuffer, Handles.ComputePipelineCascadeSetup);
    cmdPushConstants(cmdBuffer, pushConstants);
    cmdDispatchThreadGroups(cmdBuffer, {1, 1, 1});
    cmdPopMarker(cmdBuffer);
}

void PassUI(EOS::ICommandBuffer& cmdBuffer, EOS::ImGuiRenderer* UIRenderer)
{
    //Render UI
    UIRenderer->BeginFrame(cmdBuffer);
    {
        UIRenderer->SetScale(1.5f);
        ImGui::SetNextWindowSize(ImVec2(450, 520), ImGuiCond_FirstUseEver);
        ImGui::Begin("Light Settings");

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
        static const char* shadowTechniqueItems[] =
        {
            "CPU Cascade",
            "Compute Cascade",
            "RayQuery",
        };

        ImGui::Combo("Shadow Technique", &g_ShadowTechnique, shadowTechniqueItems, IM_ARRAYSIZE(shadowTechniqueItems));
        const bool isCascadeTechnique = g_ShadowTechnique == ShadowTechniqueCpuCascade || g_ShadowTechnique == ShadowTechniqueComputeCascade;
        const bool isComputeCascadeTechnique = g_ShadowTechnique == ShadowTechniqueComputeCascade;

        if (isCascadeTechnique)
        {
            ImGui::Separator();
            ImGui::Text("Cascade Controls");
            ImGui::Combo("Shadow Debug Mode", &g_ShadowDebugMode, shadowDebugModeItems, IM_ARRAYSIZE(shadowDebugModeItems));
            ImGui::SliderInt("Force Cascade", &g_ForceShadowCascade, -1, CASCADES - 1);
            if (g_ForceShadowCascade < 0)
            {
                ImGui::Text("Force Cascade: Auto");
            }

            const uint64_t shadowArrayLayerTextureID = EOS::MakeImGuiTextureID(Handles.ShadowDepthTexture, static_cast<uint32_t>(g_ShadowDebugCascadeLayer), EOS::ImGuiTextureView::Texture2DArray);
            ImGui::Image(shadowArrayLayerTextureID, {400,400});
            ImGui::SliderInt("CascadeID", &g_ShadowDebugCascadeLayer, 0, CASCADES - 1);

            if (isComputeCascadeTechnique)
            {
                ImGui::Separator();
                ImGui::Text("Compute Options");
                ImGui::Checkbox("Use Depth Reduction Range", &g_UseDepthReductionForCascades);
                ImGui::Text("Range Source: %s", g_UseDepthReductionForCascades ? "Depth Reduction" : "Camera Planes");
            }
            else
            {
                ImGui::Separator();
                ImGui::Text("CPU Cascade path active");
            }
        }
        else
        {
            ImGui::Separator();
            ImGui::Text("RayQuery mode active");
            ImGui::Text("Cascade debug/image controls are hidden in this mode.");
        }
        ImGui::End();
    }
    UIRenderer->EndFrame(cmdBuffer);
}


#pragma endregion

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

    Handles.ShaderHandleVertShade = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleFragShade = App.Context->CreateShaderModule("shade", EOS::ShaderStage::Fragment);
    Handles.ShaderHandleVertEarlyZ = App.Context->CreateShaderModule("earlyZ", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleFragEarlyZ = App.Context->CreateShaderModule("earlyZ", EOS::ShaderStage::Fragment);
    Handles.ShaderHandleVertShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Vertex);
    Handles.ShaderHandleGeomShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Geometry);
    Handles.ShaderHandleFragShadow = App.Context->CreateShaderModule("shadowDepth", EOS::ShaderStage::Fragment);
    Handles.ShaderHandleComputeDepthReduction = App.Context->CreateShaderModule("depthReduction", EOS::ShaderStage::Compute);
    Handles.ShaderHandleComputeCascadeSetup = App.Context->CreateShaderModule("cascadeSetup", EOS::ShaderStage::Compute);

    Handles.DepthTexture = App.Context->CreateTexture(
    {
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {static_cast<uint32_t>(App.Window.Width), static_cast<uint32_t>(App.Window.Height)},
        .Usage                  = EOS::TextureUsageFlags::Attachment | EOS::TextureUsageFlags::Sampled,
        .DebugName              = "Depth Buffer - CascadedShadowMapping",
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
    Handles.ShadowDepthTexture = App.Context->CreateTexture(shadowMapDescription);

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


    //Scene scene = LoadModel("../data/ABeautifulGame/ABeautifulGame.gltf", App.Context.get());
    //const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(10.00f));

    Scene scene = LoadModel("../data/sponza/Sponza.gltf", App.Context.get());
    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));

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

    Handles.AccelVertexBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::AccelStructBuildInputReadOnly,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(Vertex) * vertices.size(),
        .Data      = vertices.data(),
        .DebugName = "Buffer: accel vertex",
    });

    Handles.AccelIndexBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::AccelStructBuildInputReadOnly,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(uint32_t) * scene.indices.size(),
        .Data      = scene.indices.data(),
        .DebugName = "Buffer: accel index",
    });

    constexpr glm::mat3x4 blasTransform{1.0f};
    Handles.AccelTransformBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::AccelStructBuildInputReadOnly,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(glm::mat3x4),
        .Data      = &blasTransform,
        .DebugName = "Buffer: accel transform",
    });

    struct MeshBLASEntry
    {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t blasIndex;
    };

    std::vector<MeshBLASEntry> meshBLASCache;
    meshBLASCache.reserve(scene.meshes.size());
    Handles.SceneBLASes.reserve(scene.meshes.size());

    auto getBLASIndexForMesh = [&](const MeshEntry& mesh) -> uint32_t
    {
        for (const auto& [vertexOffset, indexOffset, indexCount, blasIndex] : meshBLASCache)
        {
            if (vertexOffset == mesh.vertexOffset && indexOffset == mesh.indexOffset && indexCount == mesh.indexCount)
            {
                return blasIndex;
            }
        }

        // EOS backend maps NumberOfVertices -> (maxVertex = NumberOfVertices - 1).
        // Pass full vertex count so Vulkan maxVertex becomes vertices.size() - 1.
        const auto globalVertexCount = static_cast<uint32_t>(vertices.size());

        Handles.SceneBLASes.emplace_back(App.Context->CreateAccelerationStructure(
        {
            .Type = EOS::BLAS,
            .GeometryType = EOS::Triangles,
            .VertexFormatStructure = EOS::VertexFormat::Float3,
            .VertexBuffer = Handles.AccelVertexBuffer,
            .VertexStride = sizeof(Vertex),
            .NumberOfVertices = globalVertexCount,
            .IndexBuffer = Handles.AccelIndexBuffer,
            .TransformBuffer = Handles.AccelTransformBuffer,
            .BuildRange =
            {
                .PrimitiveCount = mesh.indexCount / 3,
                .PrimitiveOffset = mesh.indexOffset * static_cast<uint32_t>(sizeof(uint32_t)),
                .FirstVertex = mesh.vertexOffset,
                .TransformOffset = 0,
            },
            .BuildFlags = EOS::PreferFastTrace,
            .DebugName = "Cascade Scene BLAS",
        }));

        const auto newBLASIndex = static_cast<uint32_t>(Handles.SceneBLASes.size() - 1);
        meshBLASCache.push_back({mesh.vertexOffset, mesh.indexOffset, mesh.indexCount, newBLASIndex});
        return newBLASIndex;
    };

    std::vector<EOS::AccelStructInstance> tlasInstances;
    tlasInstances.reserve(scene.meshes.size());

    for (const auto& mesh : scene.meshes)
    {
        const uint32_t blasIndex = getBLASIndexForMesh(mesh);
        const glm::mat4& mt = mesh.transform;

        tlasInstances.push_back(EOS::AccelStructInstance
        {
            .Transform =
            {
                {mt[0][0], mt[1][0], mt[2][0], mt[3][0]},
                {mt[0][1], mt[1][1], mt[2][1], mt[3][1]},
                {mt[0][2], mt[1][2], mt[2][2], mt[3][2]},
            },
            .InstanceCustomIndex = 0,
            .Mask = 0xFF,
            .InstanceShaderBindingTableRecordOffset = 0,
            .Flags = EOS::TriangleFacingCullDisable,
            .AccelerationStructureReference = App.Context->GetGPUAddress(Handles.SceneBLASes[blasIndex]),
        });
    }

    Handles.AccelInstancesBuffer = App.Context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::AccelStructBuildInputReadOnly,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = static_cast<uint64_t>(sizeof(EOS::AccelStructInstance) * tlasInstances.size()),
        .Data      = tlasInstances.data(),
        .DebugName = "Buffer: accel instances",
    });

    Handles.SceneTLAS = App.Context->CreateAccelerationStructure(
    {
        .Type = EOS::TLAS,
        .GeometryType = EOS::Instances,
        .InstancesBuffer = Handles.AccelInstancesBuffer,
        .BuildRange = {.PrimitiveCount = static_cast<uint32_t>(tlasInstances.size())},
        .BuildFlags = static_cast<uint8_t>(EOS::PreferFastTrace | EOS::AllowUpdate),
        .DebugName = "Cascade Scene TLAS",
    });

    nMeshes = scene.meshes.size();
    std::vector<DrawData> drawData = BuildDrawDataFromScene<DrawData>(scene);

    Handles.PerDrawBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DrawData) * drawData.size(),
        .Data      = drawData.data(),
        .DebugName = "PerDrawBuffer",
    });

    Handles.PerFrameBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "PerFrameBuffer",
    });

    constexpr DepthReductionData initialDepthReductionData
    {
        .minDepth = 1.0f,
        .maxDepth = 0.0f,
    };

    Handles.DepthReductionBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(DepthReductionData),
        .Data      = &initialDepthReductionData,
        .DebugName = "DepthReductionBuffer",
    });

    std::vector<EOS::DrawIndexedIndirectCommand> indirectCmds = BuildIndirectCommands(scene);

    Handles.IndirectBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::Indirect,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(EOS::DrawIndexedIndirectCommand) * indirectCmds.size(),
        .Data      = indirectCmds.data(),
        .DebugName = "IndirectDrawBuffer",
    });

    EOS::RenderPipelineDescription renderPipelineShade
    {
        .VertexInput = VertexInputDataShade,
        .VertexShader = Handles.ShaderHandleVertShade,
        .FragmentShader = Handles.ShaderHandleFragShade,
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = App.Context->GetFormat(Handles.DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipelineShade = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineEarlyZDesc
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = Handles.ShaderHandleVertEarlyZ,
        .FragmentShader = Handles.ShaderHandleFragEarlyZ,
        .DepthFormat = App.Context->GetFormat(Handles.DepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "EarlyZ Render Pipeline",
    };
    Handles.RenderPipelineEarlyZ = App.Context->CreateRenderPipeline(renderPipelineEarlyZDesc);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = Handles.ShaderHandleVertShadow,
        .GeometryShader = Handles.ShaderHandleGeomShadow,
        .FragmentShader = Handles.ShaderHandleFragShadow,
        .DepthFormat = App.Context->GetFormat(Handles.ShadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DepthClamping = true,
        .DebugName = "ShadowMap Render Pipeline",
    };
    Handles.RenderPipelineShadow = App.Context->CreateRenderPipeline(renderPipelineShadow);

    const EOS::ComputePipelineDescription computeDepthReductionDesc
    {
        .ComputeShader = Handles.ShaderHandleComputeDepthReduction,
        .DebugName = "Depth Reduction Compute Pipeline",
    };
    Handles.ComputePipelineDepthReduction = App.Context->CreateComputePipeline(computeDepthReductionDesc);

    const EOS::ComputePipelineDescription computeCascadeSetupDesc
    {
        .ComputeShader = Handles.ShaderHandleComputeCascadeSetup,
        .DebugName = "Cascade Setup Compute Pipeline",
    };
    Handles.ComputePipelineCascadeSetup = App.Context->CreateComputePipeline(computeCascadeSetupDesc);

    //Get UBO pointers
    FramePointersData =
    {
        .frameDataPtr = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .drawDataPtr = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
    };


    App.Run([&]()
    {
        const float aspectRatio = static_cast<float>(App.Window.Width) / static_cast<float>(App.Window.Height);
        if (std::isnan(aspectRatio)) return;

        //Update Light Frustum
        glm::vec3 lightForward;
        lightForward.x = cosf(glm::radians(g_LightRotation.y)) * cosf(glm::radians(g_LightRotation.x));
        lightForward.y = sinf(glm::radians(g_LightRotation.x));
        lightForward.z = sinf(glm::radians(g_LightRotation.y)) * cosf(glm::radians(g_LightRotation.x));
        lightForward = glm::normalize(lightForward);

        //Get camera matrices
        const glm::mat4 view = App.MainCamera.GetViewMatrix();
        const glm::mat4 projection = App.MainCamera.GetProjectionMatrix(aspectRatio);
        const glm::mat4 viewProjection = projection * view;
        const glm::mat4 mvp = viewProjection * m;
        const bool isCascadeTechnique = g_ShadowTechnique == ShadowTechniqueCpuCascade || g_ShadowTechnique == ShadowTechniqueComputeCascade;
        const bool useComputeCascades = g_ShadowTechnique == ShadowTechniqueComputeCascade;


        PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .view = view,
            .lightDir = glm::vec4(lightForward, 0.0f),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMapID = Handles.ShadowDepthTexture.Index(),
            .sceneTLASID = Handles.SceneTLAS.Valid() ? Handles.SceneTLAS.Index() : 0u,
            .shadowDebugMode = g_ShadowDebugMode,
            .shadowForceCascade = g_ForceShadowCascade,
            .shadowTechnique = g_ShadowTechnique,
        };

        if (!useComputeCascades)
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
        App.Context->Upload(Handles.PerFrameBuffer, &perFrameData, sizeof(PerFrameData), 0);

        // Reset min and max depth values
        if (useComputeCascades)
        {
            constexpr DepthReductionData sentinel{ 0.0f, 0.0f };
            App.Context->Upload(Handles.DepthReductionBuffer, &sentinel, sizeof(DepthReductionData), 0);
        }


        cmdPipelineBarrier(cmdBuffer, {},
{
                { swapChainTexture, EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { Handles.DepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
                { Handles.ShadowDepthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite },
            });


        PassEarlyZ(cmdBuffer);

        if (useComputeCascades)
        {
            const uint32_t gx = (static_cast<uint32_t>(App.Window.Width) + 15) / 16;
            const DepthReductionPushConstants depthReductionPC
            {
                .depthRangePtr  = App.Context->GetGPUAddress(Handles.DepthReductionBuffer),
                .depthTextureID = Handles.DepthTexture.Index(),
                .width          = static_cast<uint32_t>(App.Window.Width),
                .height         = static_cast<uint32_t>(App.Window.Height),
                .numGroupsX     = gx,
            };

            const CascadeSetupPushConstants cascadeSetupPC
            {
                .perFramePtr = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
                .depthRangePtr = App.Context->GetGPUAddress(Handles.DepthReductionBuffer),
                .invViewProjection = glm::inverse(viewProjection),
                .lightForwardNear = glm::vec4(lightForward, 0.0f),
                .cameraPlanes = glm::vec4(App.MainCamera.GetNearPlane(), App.MainCamera.GetFarPlane(), g_UseDepthReductionForCascades ? 1.0f : 0.0f, static_cast<float>(SHADOW_SIZE)),
            };

            cmdPipelineBarrier(cmdBuffer,
            {
                { Handles.DepthReductionBuffer, EOS::ResourceState::Undefined, EOS::ResourceState::UnorderedAccess },
            },
            {
                { Handles.DepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },
            });

            PassDepthReductionCompute(cmdBuffer, depthReductionPC);

            cmdPipelineBarrier(cmdBuffer,
            {
                { Handles.DepthReductionBuffer, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
                { Handles.PerFrameBuffer, EOS::ResourceState::Undefined, EOS::ResourceState::UnorderedAccess },
            },
            {});

            PassCascadeSetupCompute(cmdBuffer, cascadeSetupPC);

            cmdPipelineBarrier(cmdBuffer,
            {
                { Handles.PerFrameBuffer, EOS::ResourceState::UnorderedAccess, EOS::ResourceState::ShaderResource },
            },
            {
                { Handles.DepthTexture, EOS::ResourceState::ShaderResource, EOS::ResourceState::DepthRead },
            });
        }
        else
        {
            cmdPipelineBarrier(cmdBuffer, {},
            {
                { Handles.DepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::DepthRead },
            });
        }

        if (isCascadeTechnique)
        {
            PassShadowDepth(cmdBuffer);

            cmdPipelineBarrier(cmdBuffer, {},
            {
           { Handles.ShadowDepthTexture, EOS::ResourceState::DepthWrite, EOS::ResourceState::ShaderResource },
           { Handles.DepthTexture, EOS::ResourceState::DepthRead, EOS::ResourceState::DepthWrite },
            });
        }else
        {
            cmdPipelineBarrier(cmdBuffer, {},{{ Handles.DepthTexture, EOS::ResourceState::DepthRead, EOS::ResourceState::DepthWrite },});
        }


        PassShade(cmdBuffer, swapChainTexture);
        PassUI(cmdBuffer, App.ImGuiRenderer.get());

        cmdPipelineBarrier(cmdBuffer, {}, {{swapChainTexture, EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});

        App.Context->Submit(cmdBuffer, swapChainTexture);
    });

    scene.Cleanup();
    Handles = {};
    return 0;
}