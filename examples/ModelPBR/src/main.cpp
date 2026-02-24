#include "../../common.h"
#include "EOS.h"
#include "logger.h"
#include "shaders/shaderUtils.h"
#include "utils.h"

struct PerFrameData final
{
    glm::mat4 model;
    glm::mat4 mvp;

    glm::vec3 cameraPos;
    uint32_t  albedoID;

    uint32_t normalID;
    uint32_t metallicRoughnessID;
    uint32_t pad01;
    uint32_t pad02;
};


int main()
{
    EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Model PBR",
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

    EOS::Holder<EOS::BufferHandle> perFrameBuffer = context->CreateBuffer(
{
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(PerFrameData),
        .DebugName = "perFrameBuffer",
    });

    using glm::mat4;
    using glm::vec3;

    while (!window->ShouldClose())
    {
        window->Poll();
        if (!window->IsFocused()) continue;

        const float aspectRatio = static_cast<float>(window->Width) / static_cast<float>(window->Height);

        mat4 m = rotate(mat4(1.0f),glm::radians(90.0f) , vec3(1.0f, 0.0f, 0.0f));
        m = rotate(m, static_cast<float>(glfwGetTime()), vec3(0.0f, 0.0f, 1.0f));
        constexpr vec3 position {0.0f, 0.0f, -3.5f};
        constexpr mat4 v = glm::translate(mat4(1.0f), position);
        const mat4 p = glm::perspective(45.0f, aspectRatio, 0.1f, 1000.0f);
        const mat4 mvp = p * v * m;

        const PerFrameData perFrameData
        {
            .model = m,
            .mvp = mvp,
            .cameraPos = vec3(glm::inverse(v) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
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

        constexpr EOS::RenderPass renderPass
        {
            .Color { { .LoadOpState = EOS::LoadOp::Clear, .ClearColor = { 0.36f, 0.4f, 1.0f, 0.28f } } },
            .Depth{ .LoadOpState = EOS::LoadOp::Clear, .ClearDepth = 1.0f }
        };

        constexpr EOS::DepthState depthState
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