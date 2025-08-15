#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"


#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#define GLM_ENABLE_EXPERIMENTAL //-> TODO: Define trough cmake
#include "utils.h"
#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"


void LoadModel(const std::filesystem::path& modelPath, std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices,  EOS::IContext* context)
{
    const aiScene* scene = aiImportFile(modelPath.string().c_str(), aiProcess_Triangulate);
    CHECK(scene && scene->HasMeshes(), "Could not load mesh");

    const aiMesh* mesh = scene->mMeshes[0];

    vertices.clear();
    vertices.reserve(mesh->mNumVertices);

    indices.clear();
    indices.reserve(mesh->mNumFaces * 3);

    for (unsigned int i{}; i != mesh->mNumVertices; ++i)
    {
        const aiVector3D v = mesh->mVertices[i];
        vertices.emplace_back(glm::vec3(v.x, v.y, v.z));
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
       
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString texturePath;
            aiReturn result = material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
            
            if (result == aiReturn_SUCCESS)
            {
                EOS::Logger->warn("Diffuse texture path: {}", texturePath.C_Str());
                
                // Convert relative path to absolute if needed
                std::filesystem::path fullTexturePath = modelPath.parent_path() / texturePath.C_Str();
                
                // Load the texture
                EOS::TextureLoadingDescription albedoDescription
                {
                    .filePath = fullTexturePath,
                    .compression = EOS::Compression::BC7,
                    .context = context,
                };
                
                EOS::Holder<EOS::TextureHandle> textureData = EOS::LoadTexture(albedoDescription);
            }
        }
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

    //TODO First check if the shader is not already compiled
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleVert = EOS::LoadShader(context, shaderCompiler, "triangleVert");
    EOS::Holder<EOS::ShaderModuleHandle> shaderHandleFrag = EOS::LoadShader(context, shaderCompiler, "triangleFrag");

    constexpr EOS::VertexInputData vdesc
    {
        .Attributes    = { { .Location = 0, .Format = EOS::VertexFormat::Float3, .Offset = 0 } },
        .InputBindings = { { .Stride = sizeof(glm::vec3) } },
    };

    EOS::Holder<EOS::TextureHandle> depthTexture = context->CreateTexture(
{
        .Type                   = EOS::ImageType::Image_2D,
        .TextureFormat          = EOS::Format::Z_F32,
        .TextureDimensions      = {static_cast<uint32_t>(window->Width), static_cast<uint32_t>(window->Height)},
        .Usage                  = EOS::TextureUsageFlags::Attachment,
        .DebugName              = "Depth Buffer",
    });

    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},
        .DepthFormat = EOS::Format::Z_F32, //TODO depthTexture->Format
        .PipelineCullMode = EOS::CullMode::Back,
        .DebugName = "Basic Render Pipeline"
    };
    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = context->CreateRenderPipeline(renderPipelineDescription);

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    LoadModel("../data/rubber_duck/scene.gltf", positions, indices, context.get());

    EOS::Holder<EOS::BufferHandle> vertexBuffer = context->CreateBuffer(
    {
      .Usage     = EOS::BufferUsageFlags::Vertex,
      .Storage   = EOS::StorageType::Device,
      .Size      = sizeof(glm::vec3) * positions.size(),
      .Data      = positions.data(),
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
        const mat4 m = glm::rotate(mat4(1.0f), glm::radians(-90.0f), vec3(1, 0, 0));
        const mat4 v = glm::rotate(glm::translate(mat4(1.0f), vec3(0.0f, -0.5f, -1.5f)), static_cast<float>(glfwGetTime()), vec3(0.0f, 1.0f, 0.0f));
        const mat4 p = glm::perspective(45.0f, aspectRatio, 0.1f, 1000.0f);
        const glm::mat4 mvp = p * v * m;

        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::Present}});

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

        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
        {
            cmdPushMarker(cmdBuffer, "Render Duck", 0xff0000ff);
            cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
            cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
            cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
            cmdPushConstants(cmdBuffer, mvp);
            cmdSetDepthState(cmdBuffer, depthState);
            cmdDrawIndexed(cmdBuffer, indices.size());
            cmdPopMarker(cmdBuffer);
        }
        cmdEndRendering(cmdBuffer);
        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    return 0;
}