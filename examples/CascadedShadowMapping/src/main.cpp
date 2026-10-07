
#include "../../Common/App.h"
#include ".generated/cascadeSetup.h"
#include ".generated/depthReduction.h"
#include ".generated/shadowCommon.h"

//https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
//https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
//http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/
//https://therealmjp.github.io/posts/shadow-maps/
//https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
//https://www.researchgate.net/publication/220791941_Sample_distribution_Shadow_Maps
//https://advances.realtimerendering.com/s2010/Lauritzen-SDSM(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf

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
// PerFrameData, DrawData, FramePointers, DepthReductionData and the compute push constants are generated from the
// shaders (.generated/*.h).

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

// Position, plus the uv alpha-tested materials need to cut their holes into depth.
constexpr EOS::VertexInputData VertexInputDataShadowDepth
{
    .Attributes
    {
    { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) },
    { .Location = 1, .Format = EOS::VertexFormat::Float2, .Offset = offsetof(Vertex, uv) },
    },
    .InputBindings{{ .Stride = sizeof(Vertex) }}
};
#pragma endregion

#pragma region Handles
struct Resources final
{
    EOS::TextureHolder ShadowDepthTexture;

    EOS::BufferHolder VertexBuffer;
    EOS::BufferHolder IndexBuffer;
    EOS::BufferHolder IndirectBuffer;
    EOS::BufferHolder PerDrawBuffer;
    EOS::BufferHolder PerFrameBuffer;
    EOS::BufferHolder AccelVertexBuffer;
    EOS::BufferHolder AccelIndexBuffer;
    EOS::BufferHolder AccelTransformBuffer;
    EOS::BufferHolder AccelInstancesBuffer;

    std::vector<EOS::AccelStructHolder> SceneBLASes;
    EOS::AccelStructHolder SceneTLAS;

    EOS::RenderPipelineHolder RenderPipelineEarlyZ;
    EOS::RenderPipelineHolder RenderPipelineEarlyZAlphaTested;
    EOS::RenderPipelineHolder RenderPipelineShade;
    EOS::RenderPipelineHolder RenderPipelineShadow;
    EOS::RenderPipelineHolder RenderPipelineShadowAlphaTested;
    EOS::ComputePipelineHolder ComputePipelineDepthReduction;
    EOS::ComputePipelineHolder ComputePipelineCascadeSetup;

    EOS::SamplerHolder DepthMapSampler;

    EOS::ShaderProgramHolder ShadeShader;
    EOS::ShaderProgramHolder EarlyZShader;
    EOS::ShaderProgramHolder ShadowShader;
    EOS::ShaderProgramHolder DepthReductionShader;
    EOS::ShaderProgramHolder CascadeSetupShader;
};

Resources Handles;
#pragma endregion

FramePointers FramePointersData;

int32_t nMeshes;
uint32_t nOpaqueMeshes;     // meshes [0, nOpaqueMeshes) are opaque, the rest alpha-tested (see PartitionMeshesByAlphaTest)

// Lights
glm::vec2 g_LightRotation     = {-73, -90};

// Cascades
std::array<Cascade, CASCADES> g_Cascades;
int g_ShadowDebugCascadeLayer = 0;
int g_ShadowDebugMode = 0;
int g_ForceShadowCascade = -1;
bool g_UseDepthReductionForCascades = true;

int g_ShadowTechnique = static_cast<int>(ShadowTechnique::CpuCascade);   // an int for the UI combo

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
// Depth-only passes draw the opaque meshes with a fragment shader that never discards, which keeps the fast depth-only
// path, and only the alpha-tested meshes with the one that does. Draw indices restart at 0 in the second indirect draw,
// so its push constants point the draw data at the first alpha-tested mesh.
void DrawDepthSplitByAlphaTest(EOS::ICommandBuffer& cmdBuffer, EOS::RenderPipelineHandle opaquePipeline, EOS::RenderPipelineHandle alphaTestedPipeline)
{
    const uint32_t nAlphaTestedMeshes = static_cast<uint32_t>(nMeshes) - nOpaqueMeshes;

    if (nOpaqueMeshes > 0)
    {
        cmdBindRenderPipeline(cmdBuffer, opaquePipeline);
        cmdPushConstants(cmdBuffer, FramePointersData);
        cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, 0, nOpaqueMeshes);
    }

    if (nAlphaTestedMeshes > 0)
    {
        const FramePointers alphaTestedPointers
        {
            .perFrame = FramePointersData.perFrame,
            .draws = FramePointersData.draws + nOpaqueMeshes * sizeof(DrawData),
        };

        cmdBindRenderPipeline(cmdBuffer, alphaTestedPipeline);
        cmdPushConstants(cmdBuffer, alphaTestedPointers);
        cmdDrawIndexedIndirect(cmdBuffer, Handles.IndirectBuffer, nOpaqueMeshes * sizeof(EOS::DrawIndexedIndirectCommand), nAlphaTestedMeshes);
    }
}

void AddEarlyZPass(EOS::RenderGraph& graph, EOS::GraphTexture depth, EOS::GraphBuffer perFrame)
{
    graph.AddRasterPass("Early-Z").Depth(EOS::ClearDepth(depth)).Read(perFrame).Execute([](EOS::PassContext& pass)
    {
        cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
        cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
        cmdSetDepthState(pass.Cmd, DepthStateWrite);
        DrawDepthSplitByAlphaTest(pass.Cmd, Handles.RenderPipelineEarlyZ, Handles.RenderPipelineEarlyZAlphaTested);
    });
}

// Fits the cascades to the depth range the early-Z pass left behind, on the GPU: a reduction finds the visible depth
// range, and a single thread turns it into the cascade matrices in PerFrameData.
void AddComputeCascadePasses(EOS::RenderGraph& graph, EOS::GraphTexture depth, EOS::GraphBuffer perFrame, CascadeSetupPushConstants cascadeSetup)
{
    const EOS::GraphBuffer depthRange = graph.CreateBuffer({.Size = sizeof(DepthReductionData), .DebugName = "Depth Range"});

    graph.AddTransferPass("Reset Depth Range").CopyTo(depthRange).Execute([depthRange](EOS::PassContext& pass)
    {
        // Identity values for the atomic min/max: min starts at the far plane, max at the near plane.
        constexpr DepthReductionData sentinel{.minDepth = 1.0f, .maxDepth = 0.0f};
        cmdUpdateBuffer(pass.Cmd, pass.Buffer(depthRange), sentinel);
    });

    graph.AddComputePass("Depth Reduction").Sample(depth).Write(depthRange).Execute([depth, depthRange](EOS::PassContext& pass)
    {
        const EOS::Dimensions size = pass.Size(depth);
        const DepthReductionPushConstants pushConstants
        {
            .depthRange   = pass.Address(depthRange),
            .depthTexture = pass.Descriptor(depth),
            .width        = size.Width,
            .height       = size.Height,
            .numGroupsX   = (size.Width + 15) / 16,
        };

        cmdBindComputePipeline(pass.Cmd, Handles.ComputePipelineDepthReduction);
        cmdPushConstants(pass.Cmd, pushConstants);
        cmdDispatchThreadGroups(pass.Cmd, {pushConstants.numGroupsX, (size.Height + 15) / 16, 1});
    });

    graph.AddComputePass("Cascade Setup").Read(depthRange).Write(perFrame).Execute([depthRange, perFrame, cascadeSetup](EOS::PassContext& pass) mutable
    {
        cascadeSetup.perFrame = pass.Address(perFrame);
        cascadeSetup.depthRange = pass.Address(depthRange);

        cmdBindComputePipeline(pass.Cmd, Handles.ComputePipelineCascadeSetup);
        cmdPushConstants(pass.Cmd, cascadeSetup);
        cmdDispatchThreadGroups(pass.Cmd, {1, 1, 1});
    });
}

void AddShadowPass(EOS::RenderGraph& graph, EOS::GraphTexture shadowMap, EOS::GraphBuffer perFrame)
{
    // Every cascade is a layer of the shadow map; the geometry shader routes each triangle to its layers.
    graph.AddRasterPass("Shadow")
        .Depth({.Texture = shadowMap, .Load = EOS::LoadOp::Clear, .LayerCount = CASCADES})
        .Read(perFrame)
        .Execute([](EOS::PassContext& pass)
    {
        cmdBindVertexBuffer(pass.Cmd, 0, Handles.VertexBuffer);
        cmdBindIndexBuffer(pass.Cmd, Handles.IndexBuffer, EOS::IndexFormat::UI32);
        cmdSetDepthState(pass.Cmd, DepthStateWrite);
        DrawDepthSplitByAlphaTest(pass.Cmd, Handles.RenderPipelineShadow, Handles.RenderPipelineShadowAlphaTested);
    });
}

// Shades exactly the surfaces the early-Z pass kept (depth test Equal, no depth writes). shadowMap is invalid when ray
// queries make the shadows.
void AddShadePass(EOS::RenderGraph& graph, EOS::GraphTexture backbuffer, EOS::GraphTexture depth, EOS::GraphTexture shadowMap, EOS::GraphBuffer perFrame)
{
    EOS::PassBuilder pass = graph.AddRasterPass("Shade")
        .Color(EOS::Clear(backbuffer, {0.36f, 0.4f, 1.0f, 0.28f}))
        .Depth(EOS::LoadDepth(depth, true))
        .Read(perFrame);
    if (shadowMap.Valid()) pass.Sample(shadowMap);

    pass.Execute([](EOS::PassContext& context)
    {
        cmdBindRenderPipeline(context.Cmd, Handles.RenderPipelineShade);
        cmdPushConstants(context.Cmd, FramePointersData);
        cmdSetDepthState(context.Cmd, DepthStateRead);
        cmdDrawIndexedIndirect(context.Cmd, Handles.IndirectBuffer, 0, nMeshes);
    });
}

void DeclareUI(ExampleApp& App)
{
    App.UIRenderer->SetScale(1.5f);
    EOS::UI::SetNextWindowSize(450, 520);
    EOS::UI::Begin("Light Settings");

    EOS::UI::DragFloat2("Light Rotation", glm::value_ptr(g_LightRotation));
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

    EOS::UI::Combo("Shadow Technique", &g_ShadowTechnique, shadowTechniqueItems);
    const ShadowTechnique technique = static_cast<ShadowTechnique>(g_ShadowTechnique);
    const bool isCascadeTechnique = technique == ShadowTechnique::CpuCascade || technique == ShadowTechnique::ComputeCascade;
    const bool isComputeCascadeTechnique = technique == ShadowTechnique::ComputeCascade;

    if (isCascadeTechnique)
    {
        EOS::UI::Separator();
        EOS::UI::Text("Cascade Controls");
        EOS::UI::Combo("Shadow Debug Mode", &g_ShadowDebugMode, shadowDebugModeItems);
        EOS::UI::SliderInt("Force Cascade", &g_ForceShadowCascade, -1, CASCADES - 1);
        if (g_ForceShadowCascade < 0)
        {
            EOS::UI::Text("Force Cascade: Auto");
        }

        const uint64_t shadowArrayLayerTextureID = EOS::UI::MakeTextureID(Handles.ShadowDepthTexture, static_cast<uint32_t>(g_ShadowDebugCascadeLayer), EOS::UI::TextureView::Texture2DArray);
        EOS::UI::Image(shadowArrayLayerTextureID, 400,400);
        EOS::UI::SliderInt("CascadeID", &g_ShadowDebugCascadeLayer, 0, CASCADES - 1);

        if (isComputeCascadeTechnique)
        {
            EOS::UI::Separator();
            EOS::UI::Text("Compute Options");
            EOS::UI::Checkbox("Use Depth Reduction Range", &g_UseDepthReductionForCascades);
            EOS::UI::Text("Range Source: %s", g_UseDepthReductionForCascades ? "Depth Reduction" : "Camera Planes");
        }
        else
        {
            EOS::UI::Separator();
            EOS::UI::Text("CPU Cascade path active");
        }
    }
    else
    {
        EOS::UI::Separator();
        EOS::UI::Text("RayQuery mode active");
        EOS::UI::Text("Cascade debug/image controls are hidden in this mode.");
    }
    EOS::UI::End();
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

    Handles.ShadeShader = App.Context->CreateShaderProgram({.Module = "shade"});
    Handles.EarlyZShader = App.Context->CreateShaderProgram({.Module = "earlyZ"});
    Handles.ShadowShader = App.Context->CreateShaderProgram({.Module = "shadowDepth"});
    Handles.DepthReductionShader = App.Context->CreateShaderProgram({.Module = "depthReduction"});
    Handles.CascadeSetupShader = App.Context->CreateShaderProgram({.Module = "cascadeSetup"});

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
    nOpaqueMeshes = PartitionMeshesByAlphaTest(scene);

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
    std::vector<DrawData> drawData = BuildDrawDataFromScene<DrawData>(scene, App.DefaultSampler);

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
        .VertexShader = {Handles.ShadeShader, "vertexMain"},
        .FragmentShader = {Handles.ShadeShader, "fragmentMain"},
        .ColorAttachments = {{ .ColorFormat = App.Context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32,
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    Handles.RenderPipelineShade = App.Context->CreateRenderPipeline(renderPipelineShade);

    EOS::RenderPipelineDescription renderPipelineEarlyZDesc
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = {Handles.EarlyZShader, "vertexMain"},
        .FragmentShader = {Handles.EarlyZShader, "fragmentOpaque"},
        .DepthFormat = EOS::Format::Z_F32,
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "EarlyZ Render Pipeline",
    };
    Handles.RenderPipelineEarlyZ = App.Context->CreateRenderPipeline(renderPipelineEarlyZDesc);

    renderPipelineEarlyZDesc.FragmentShader = {Handles.EarlyZShader, "fragmentMasked"};
    renderPipelineEarlyZDesc.DebugName = "EarlyZ Alpha Tested Render Pipeline";
    Handles.RenderPipelineEarlyZAlphaTested = App.Context->CreateRenderPipeline(renderPipelineEarlyZDesc);

    EOS::RenderPipelineDescription renderPipelineShadow
    {
        .VertexInput = VertexInputDataShadowDepth,
        .VertexShader = {Handles.ShadowShader, "vertexMain"},
        .GeometryShader = {Handles.ShadowShader, "geometryMain"},
        .FragmentShader = {Handles.ShadowShader, "fragmentOpaque"},
        .DepthFormat = App.Context->GetFormat(Handles.ShadowDepthTexture),
        .PipelineCullMode = EOS::CullMode::Back,
        .DepthClamping = true,
        .DebugName = "ShadowMap Render Pipeline",
    };
    Handles.RenderPipelineShadow = App.Context->CreateRenderPipeline(renderPipelineShadow);

    renderPipelineShadow.FragmentShader = {Handles.ShadowShader, "fragmentMasked"};
    renderPipelineShadow.DebugName = "ShadowMap Alpha Tested Render Pipeline";
    Handles.RenderPipelineShadowAlphaTested = App.Context->CreateRenderPipeline(renderPipelineShadow);

    const EOS::ComputePipelineDescription computeDepthReductionDesc
    {
        .ComputeShader = {Handles.DepthReductionShader, "computeMain"},
        .DebugName = "Depth Reduction Compute Pipeline",
    };
    Handles.ComputePipelineDepthReduction = App.Context->CreateComputePipeline(computeDepthReductionDesc);

    const EOS::ComputePipelineDescription computeCascadeSetupDesc
    {
        .ComputeShader = {Handles.CascadeSetupShader, "computeMain"},
        .DebugName = "Cascade Setup Compute Pipeline",
    };
    Handles.ComputePipelineCascadeSetup = App.Context->CreateComputePipeline(computeCascadeSetupDesc);

    //Get UBO pointers
    FramePointersData =
    {
        .perFrame = App.Context->GetGPUAddress(Handles.PerFrameBuffer),
        .draws = App.Context->GetGPUAddress(Handles.PerDrawBuffer),
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
        const ShadowTechnique technique = static_cast<ShadowTechnique>(g_ShadowTechnique);
        const bool isCascadeTechnique = technique == ShadowTechnique::CpuCascade || technique == ShadowTechnique::ComputeCascade;
        const bool useComputeCascades = technique == ShadowTechnique::ComputeCascade;


        PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .view = view,
            .lightDir = glm::vec4(lightForward, 0.0f),
            .cameraPos = App.MainCamera.GetPosition(),
            .shadowMap = Handles.ShadowDepthTexture,
            .shadowSampler = Handles.DepthMapSampler,
            .sceneTLAS = Handles.SceneTLAS,
            .shadowDebugMode = g_ShadowDebugMode,
            .shadowForceCascade = g_ForceShadowCascade,
            .shadowTechnique = technique,
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

        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
        const EOS::GraphTexture depth = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
        const EOS::GraphBuffer perFrame = graph.ImportBuffer(Handles.PerFrameBuffer, "PerFrameBuffer");
        // Owned by the example rather than the graph: PerFrameData and the UI refer to it before the graph executes.
        const EOS::GraphTexture shadowMap = isCascadeTechnique ? graph.ImportTexture(Handles.ShadowDepthTexture, "Shadow Map") : EOS::GraphTexture{};

        graph.AddUpload("Upload Frame Data", perFrame, perFrameData);
        AddEarlyZPass(graph, depth, perFrame);

        if (useComputeCascades)
        {
            AddComputeCascadePasses(graph, depth, perFrame,
            {
                .invViewProjection = glm::inverse(viewProjection),
                .lightForwardNear = glm::vec4(lightForward, 0.0f),
                .cameraPlanes = glm::vec4(App.MainCamera.GetNearPlane(), App.MainCamera.GetFarPlane(), g_UseDepthReductionForCascades ? 1.0f : 0.0f, static_cast<float>(SHADOW_SIZE)),
            });
        }

        if (isCascadeTechnique) AddShadowPass(graph, shadowMap, perFrame);
        AddShadePass(graph, backbuffer, depth, shadowMap, perFrame);

        EOS::PassBuilder uiPass = App.AddUIPass(backbuffer, [&] { DeclareUI(App); });
        if (isCascadeTechnique) uiPass.Sample(shadowMap);

        graph.Execute();
    });

    scene.Cleanup();
    Handles = {};
    return 0;
}