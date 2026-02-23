#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"

#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "utils.h"
#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct TextureHandles final
{
    EOS::Holder<EOS::TextureHandle> albedo;
    EOS::Holder<EOS::TextureHandle> normal;
    EOS::Holder<EOS::TextureHandle> metallicRoughness;
};

void LoadModel(const std::filesystem::path& modelPath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, TextureHandles& handles , EOS::IContext* context)
{
    const aiScene* scene = aiImportFile(
       modelPath.string().c_str(),
       aiProcess_Triangulate
   );
    CHECK(scene && scene->HasMeshes(), "Could not load mesh: {}", modelPath.c_str());

    const aiMesh* mesh = scene->mMeshes[0];

    vertices.clear();
    vertices.reserve(mesh->mNumVertices);

    indices.clear();
    indices.reserve(mesh->mNumFaces * 3);

    for (unsigned int i{}; i != mesh->mNumVertices; ++i)
    {
        const aiVector3D v = mesh->mVertices[i];
        const aiVector3D uv = mesh->mTextureCoords[0][i];
        const aiVector3D n = mesh->mNormals[i];
        vertices.emplace_back(glm::vec3(v.x, v.y, v.z), glm::vec3(n.x, n.y, n.z),glm::vec2(uv.x, 1 - uv.y));
    }

    for (unsigned int i{}; i != mesh->mNumFaces; ++i)
    {
        for (int j{}; j != 3; ++j)
        {
            indices.emplace_back(mesh->mFaces[i].mIndices[j]);
        }
    }

    // Extract texture path from material
    if (scene->mNumMaterials > 0 && mesh->mMaterialIndex < scene->mNumMaterials)
    {
        const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        //Lambda for texture extraction
        auto loadTextureOfType = [&](aiTextureType textureType) -> EOS::Holder<EOS::TextureHandle>
        {
            if (material->GetTextureCount(textureType) > 0)
            {
                aiString texturePath;
                aiReturn result = material->GetTexture(textureType, 0, &texturePath);

                if (result == aiReturn_SUCCESS)
                {
                    // Convert relative path to absolute if needed
                    std::filesystem::path fullTexturePath = modelPath.parent_path() / texturePath.C_Str();

                    // Load the texture
                    EOS::TextureLoadingDescription textureDescription
                    {
                        .filePath = fullTexturePath,
                        .compression = textureType == aiTextureType_NORMALS ? EOS::Compression::BC5 : EOS::Compression::BC7,
                        .context = context,
                    };

                    return EOS::LoadTexture(textureDescription);
                }
            }

            CHECK(false, "Could not find texture");
            return {};
        };

        handles.albedo = loadTextureOfType(aiTextureType_DIFFUSE);
        handles.normal = loadTextureOfType(aiTextureType_NORMALS);
        handles.metallicRoughness = loadTextureOfType(aiTextureType_GLTF_METALLIC_ROUGHNESS);
    }
}

int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Window",
    };

    std::unique_ptr<EOS::Window> window = std::make_unique<EOS::Window>(contextDescr);
    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapChain(contextDescr);
    std::unique_ptr<EOS::ShaderCompiler> shaderCompiler = EOS::CreateShaderCompiler("./");
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(context, shaderCompiler, "modelAlbedo", EOS::ShaderStage::Vertex);
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(context, shaderCompiler, "modelAlbedo", EOS::ShaderStage::Fragment);

    //TODO: This could be constevaled with reflection
    constexpr EOS::VertexInputData vdesc
    {
        .Attributes =
    {
            { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, position) },
            { .Location = 1, .Format = EOS::VertexFormat::Float3, .Offset = offsetof(Vertex, normal) },
            { .Location = 2, .Format = EOS::VertexFormat::Float2, .Offset = offsetof(Vertex, uv) }
        },

        .InputBindings =
        {
            { .Stride = sizeof(Vertex) }
        }
    };

    EOS::Holder<EOS::TextureHandle> depthTexture = context->CreateTexture(
{
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {static_cast<uint32_t>(window->Width), static_cast<uint32_t>(window->Height)},
        .Usage                  = EOS::TextureUsageFlags::Attachment,
        .DebugName              = "Depth Buffer",
    });

    EOS::SamplerDescription samplerDescription
    {
        .mipLodMax = EOS_MAX_MIP_LEVELS,
        .maxAnisotropic = 0,
        .debugName = "Linear Sampler",
    };
    EOS::Holder<EOS::SamplerHandle> sampler = context->CreateSampler(samplerDescription);

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32, //TODO depthTexture->Format
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline",
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = context->CreateRenderPipeline(renderPipelineDescription);

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    TextureHandles handles;
    LoadModel("../data/damaged_helmet/DamagedHelmet.gltf", vertices, indices,handles, context.get());
    EOS::Holder<EOS::BufferHandle> vertexBuffer = context->CreateBuffer(
    {
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(Vertex) * vertices.size(),
      .Data      = vertices.data(),
      .DebugName = "Buffer: vertex"
      });
    EOS::Holder<EOS::BufferHandle> indexBuffer = context->CreateBuffer(
    {
        .Usage     = EOS::BufferUsageFlags::Index,
        .Storage   = EOS::StorageType::Device,
        .Size      = sizeof(uint32_t) * indices.size(),
        .Data      = indices.data(),
        .DebugName = "Buffer: index"
    });

    struct PerFrameData final
    {
        glm::mat4 model;
        glm::mat4 mvp;
        glm::vec3 cameraPos;
        uint32_t albedoID;
        uint32_t normalID;
        uint32_t metallicRoughnessID;
    };

    EOS::Holder<EOS::BufferHandle> perFrameBuffer = context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "perFrameBuffer",
    });

    while (!window->ShouldClose())
    {
        window->Poll();
        if (!window->IsFocused())
        {
            continue; // Or sleep
        }
        const float aspectRatio = static_cast<float>(window->Width) / static_cast<float>(window->Height);

        using glm::mat4;
        using glm::vec3;
        constexpr vec3 position {0.0f, 0.0f, -3.5f};
        const mat4 m = glm::rotate(mat4(1.0f), glm::radians(90.0f), vec3(1, 0, 0));
        const mat4 v = glm::rotate(glm::translate(mat4(1.0f), position), static_cast<float>(glfwGetTime()), vec3(0.0f, 1.0f, 0.0f));
        const mat4 p = glm::perspective(45.0f, aspectRatio, 0.1f, 1000.0f);
        const glm::mat4 mvp = p * v * m;


        const PerFrameData perFrameData
        {
            .model = glm::transpose(glm::inverse(m)),
            .mvp = mvp,
            .cameraPos = position,
            .albedoID = handles.albedo.Index(),
            .normalID = handles.normal.Index(),
            .metallicRoughnessID = handles.metallicRoughness.Index(),
        };


        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        EOS::Framebuffer framebuffer
        {
            .Color = {{.Texture = context->GetSwapChainTexture()}},
            .DepthStencil = { .Texture = depthTexture },
            .DebugName = "Basic Color Depth Framebuffer"
        };

        EOS::RenderPass renderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
            .Depth{ .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
        };

        EOS::DepthState depthState
        {
            .CompareOpState = EOS::CompareOp::Less,
            .IsDepthWriteEnabled = true,
        };


        cmdPipelineBarrier(cmdBuffer, {},
            {
                { context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::RenderTarget },
                { depthTexture, EOS::ResourceState::Undefined, EOS::ResourceState::DepthWrite }
            });

        cmdUpdateBuffer(cmdBuffer, perFrameBuffer, perFrameData);
        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Damaged Helmet", 0xff0000ff);
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);

            struct PerFrameData
            {
                uint64_t draw;
            }pc
            {
                .draw = context->GetGPUAddress(perFrameBuffer)
            };

            cmdPushConstants(cmdBuffer, pc);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexed(cmdBuffer, indices.size());
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);

        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::RenderTarget, EOS::ResourceState::Present}});
        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    return 0;
}