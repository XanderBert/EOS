#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"


#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"


void LoadModel(const std::filesystem::path& modelPath, std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices)
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

    aiReleaseImport(scene);
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


    //It would be nice if these pipeline descriptions would be stored as JSON/XML into the material system
    EOS::RenderPipelineDescription renderPipelineDescription
    {
        .VertexInput = vdesc,
        .VertexShader = shaderHandleVert,
        .FragmentShader = shaderHandleFrag,
        .ColorAttachments = {{ .ColorFormat = context->GetSwapchainFormat()}},

    };

    EOS::Holder<EOS::RenderPipelineHandle> renderPipelineHandle = context->CreateRenderPipeline(renderPipelineDescription);

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;

    //TODO: Copy over data to bin
    LoadModel("../data/rubber_duck/scene.gltf", positions, indices);



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

        //TODO: There is no depth texture available in the vertex stage to handle Depth Comparrison

        glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(  static_cast<float>(glfwGetTime() * 10.0f)  ), glm::vec3(1, 0, 0));
        constexpr glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, -1.5f));
        const glm::mat4 projection = glm::perspective(glm::radians(65.0f), aspectRatio, 0.1f, 1000.0f);
        const glm::mat4 mvp = projection * view * model;

        EOS::ICommandBuffer& cmdBuffer = context->AcquireCommandBuffer();
        cmdPipelineBarrier(cmdBuffer, {}, {{context->GetSwapChainTexture(), EOS::ResourceState::Undefined, EOS::ResourceState::Present}});

        EOS::Framebuffer framebuffer = {.Color = {{.Texture = context->GetSwapChainTexture()}}};
        EOS::RenderPass renderPass{ .Color = { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } }};
        cmdBeginRendering(cmdBuffer, renderPass, framebuffer);
            cmdPushMarker(cmdBuffer, "Render Duck", 0xff0000ff);
                cmdBindVertexBuffer(cmdBuffer, 0, vertexBuffer);
                cmdBindIndexBuffer(cmdBuffer, indexBuffer, EOS::IndexFormat::UI32);
                cmdBindRenderPipeline(cmdBuffer, renderPipelineHandle);
                cmdPushConstants(cmdBuffer, mvp);
                cmdDrawIndexed(cmdBuffer, indices.size());
            cmdPopMarker(cmdBuffer);
        cmdEndRendering(cmdBuffer);

        context->Submit(cmdBuffer, context->GetSwapChainTexture());
    }

    return 0;
}