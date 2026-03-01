#pragma once
#include "EOS.h"
#include "utils.h"
#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"
#include "shaders/shaderUtils.h"



struct InputState final
{
    bool forward{};
    bool backward{};
    bool left{};
    bool right{};
    bool rightMouse{};
};

struct CameraDescription final
{
    glm::vec3 origin        = glm::vec3(0.0f);
    glm::vec2 rotation      = glm::vec2(0.0f);
    float fov               = 45.0f;
    float acceleration      = 45.0f;
    float damping           = 5.0f;
    float near              = 0.1f;
    float far               = 100.0f;
};

struct Camera final
{
    explicit Camera(const CameraDescription& cameraDescription)
    : Pitch(cameraDescription.rotation.x)
    , Yaw(cameraDescription.rotation.y)
    , Position(cameraDescription.origin)
    , Acceleration(cameraDescription.acceleration)
    , Damping(cameraDescription.damping)
    , Fov(cameraDescription.fov)
    , Near(cameraDescription.near)
    , Far(cameraDescription.far)
    {}

    [[nodiscard]] glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position, Position + GetForward(), Up);
    }

    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspectRatio) const
    {
        return glm::perspective(glm::radians(Fov), aspectRatio, Near, Far);
    }

    [[nodiscard]] glm::mat4 GetViewProjectionMatrix(float aspectRatio) const
    {
        return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
    }

    [[nodiscard]] glm::vec3 GetPosition() const
    {
        return Position;
    }

    void Update(const glm::vec3& direction, float deltaTime)
    {
        //Update Position
        const glm::vec3 forward = GetForward();
        const glm::vec3 right = GetRight();
        const glm::vec3 up = Up;
        const glm::mat3 basis(right, up, forward);

        const float dampingFactor = std::exp(-Damping * deltaTime);
        Velocity *= dampingFactor;

        glm::vec3 desiredDir = basis * direction;
        if (glm::length(desiredDir) > 0.0f)
        {
            desiredDir = glm::normalize(desiredDir);
            Velocity += desiredDir * deltaTime * Acceleration;
        }
        Position += Velocity * deltaTime;
    }

    float Pitch{};
    float Yaw{};

private:
    [[nodiscard]] glm::vec3 GetForward() const
    {
        glm::vec3 forward;
        forward.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        forward.y = sin(glm::radians(Pitch));
        forward.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        return glm::normalize(forward);
    }

    [[nodiscard]] glm::vec3 GetRight() const
    {
        return glm::normalize(glm::cross(GetForward(), Up));
    }

    glm::vec3 Position;
    glm::vec3 Up {0.0f, 1.0f, 0.0f};
    glm::vec3 Velocity{};

    float Acceleration  = 45.0f;
    float Damping  = 5.0f;

    float Fov;
    float Near;
    float Far;
};

struct ExampleAppDescription final
{
    EOS::ContextCreationDescription contextDescription;
    CameraDescription cameraDescription;
};

class ExampleApp final
{
public:
    explicit ExampleApp(ExampleAppDescription& appDescription)
    :   MainCamera(appDescription.cameraDescription)
    ,   Window(appDescription.contextDescription)
    ,   Context(EOS::CreateContextWithSwapChain(appDescription.contextDescription))
    ,   ShaderCompiler(EOS::CreateShaderCompiler("./"))
    {
        //Create Default Sampler
        constexpr EOS::SamplerDescription samplerDescription
        {
            .mipLodMax = EOS_MAX_MIP_LEVELS,
            .maxAnisotropic = 0,
            .debugName = "Linear Sampler",
        };
        DefaultSampler = Context->CreateSampler(samplerDescription);
        SetupInputCallbacks();
    }

    DELETE_COPY_MOVE(ExampleApp)

    [[nodiscard]] EOS::Holder<EOS::TextureHandle> CreateDepthTexture() const
    {
        return Context->CreateTexture(
        {
            .Type                   = EOS::ImageType::Image_2D,
            .TextureFormat          = EOS::Format::Z_F32,
            .TextureDimensions      = {static_cast<uint32_t>(Window.Width), static_cast<uint32_t>(Window.Height)},
            .Usage                  = EOS::TextureUsageFlags::Attachment,
            .DebugName              = "Depth Buffer",
        });
    }

    Camera MainCamera;
    EOS::Window Window;
    std::unique_ptr<EOS::IContext> Context;
    std::unique_ptr<EOS::ShaderCompiler> ShaderCompiler;
    EOS::Holder<EOS::SamplerHandle> DefaultSampler;
    InputState Input;
    float DeltaTime{};

    template <typename Function>
    void Run(Function&& renderLoop)
    {
        lastTime = glfwGetTime();

        while (!Window.ShouldClose())
        {
            Window.Poll();
            if (!Window.IsFocused()) continue;

            //Update time
            const float currentTime = glfwGetTime();
            DeltaTime = currentTime - lastTime;
            lastTime = currentTime;

            //Update Camera
            glm::vec3 direction{Input.right - Input.left, 0.0f, Input.forward - Input.backward};
            MainCamera.Update(direction, DeltaTime);


            //Render
            std::forward<Function>(renderLoop)();
        }
    }

private:

    void SetupInputCallbacks()
    {
        glfwSetWindowUserPointer(Window.GlfwWindow, this);
        glfwSetKeyCallback(Window.GlfwWindow, [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            ExampleApp* app = static_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
            if (!app)return;

            const bool pressed = action != GLFW_RELEASE;
            app->Input.forward = key == GLFW_KEY_W && pressed;
            app->Input.backward = key == GLFW_KEY_S && pressed;
            app->Input.left = key == GLFW_KEY_A && pressed;
            app->Input.right = key == GLFW_KEY_D && pressed;
        });
        glfwSetMouseButtonCallback(Window.GlfwWindow, [](GLFWwindow* window, int button, int action, int mods)
        {
            ExampleApp* app = static_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
            if (!app) return;

            const bool pressed = action != GLFW_RELEASE;
            app->Input.rightMouse = button == GLFW_MOUSE_BUTTON_1 && pressed;
        });
        glfwSetCursorPosCallback(Window.GlfwWindow, [](GLFWwindow* window, double xpos, double ypos)
        {
            ExampleApp* app = static_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
            if (!app) return;
            if (!app->Input.rightMouse) return;

            static bool firstMouse = true;
            static double lastX = xpos;
            static double lastY = ypos;

            if (firstMouse)
            {
                lastX = xpos;
                lastY = ypos;
                firstMouse = false;
            }

            float xoffset = static_cast<float>(xpos - lastX);
            float yoffset = static_cast<float>(lastY - ypos); // reversed since y-coords go down
            lastX = xpos;
            lastY = ypos;

            constexpr float mouseSensitivity = 0.1f;
            xoffset *= mouseSensitivity;
            yoffset *= mouseSensitivity;

            app->MainCamera.Yaw   += xoffset;
            app->MainCamera.Pitch += yoffset;

            // clamp pitch so we don't flip upside down
            if (app->MainCamera.Pitch > 89.0f)  app->MainCamera.Pitch = 89.0f;
            if (app->MainCamera.Pitch < -89.0f) app->MainCamera.Pitch = -89.0f;
        });
    }
    float lastTime{};
};


struct TextureHandles final
{
    EOS::Holder<EOS::TextureHandle> albedo;
    EOS::Holder<EOS::TextureHandle> normal;
    EOS::Holder<EOS::TextureHandle> metallicRoughness;
};

struct MeshEntry final
{
    uint32_t vertexOffset;  // offset into the vertex buffer
    uint32_t indexOffset;   // offset into the index buffer
    uint32_t indexCount;
    uint32_t drawDataIndex; // index into the DrawData buffer
    TextureHandles textures;
    glm::mat4 transform{};
};

struct VertexInformation final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::vec4 color;
};

struct Scene final
{
    std::vector<VertexInformation> vertices;
    std::vector<uint32_t>  indices;
    std::vector<MeshEntry> meshes;
};

inline Scene LoadModel(const std::filesystem::path& modelPath, EOS::IContext* context)
{
    const aiScene* scene = aiImportFile(modelPath.string().c_str(),aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_OptimizeMeshes);
    CHECK(scene && scene->HasMeshes(), "Could not load mesh: {}", modelPath.string());

    //Calculate vertices and indices
    const int nMeshes = scene->mNumMeshes;
    int numVertices{};
    int numIndices{};
    for (int i{}; i < nMeshes; ++i)
    {
        numVertices += scene->mMeshes[i]->mNumVertices;
        numIndices += scene->mMeshes[i]->mNumFaces * 3;
    }

    Scene importedScene{};
    importedScene.meshes.resize(nMeshes);
    importedScene.vertices.reserve(numVertices);
    importedScene.indices.reserve(numIndices);

    for (int meshIdx{}; meshIdx < nMeshes; ++meshIdx)
    {
        MeshEntry& meshEntry = importedScene.meshes[meshIdx];
        const aiMesh* mesh = scene->mMeshes[meshIdx];

        meshEntry.vertexOffset = importedScene.vertices.size();
        meshEntry.indexOffset = importedScene.indices.size();

        const aiColor4D* colors = mesh->mColors[0];

        for (unsigned int i{}; i != mesh->mNumVertices; ++i)
        {
            const aiVector3D v  = mesh->mVertices[i];
            const aiVector3D uv = mesh->mTextureCoords[0][i];
            const aiVector3D n  = mesh->mNormals[i];
            const aiVector3D t  = mesh->mTangents[i];

            VertexInformation vertex
            {
                .position       = glm::vec3(v.x, v.y, v.z),
                .normal         = glm::vec3(n.x, n.y, n.z),
                .uv             = glm::vec2(uv.x, 1 - uv.y),
                .tangent        = glm::vec4{t.x, t.y, t.z, 1.0f},
                .color          = colors ? glm::vec4(colors[i].r, colors[i].g, colors[i].b, colors[i].a) : glm::vec4(1),
            };

            importedScene.vertices.emplace_back(vertex);
        }



        for (unsigned int i{}; i != mesh->mNumFaces; ++i)
        {
            for (int j{}; j != 3; ++j)
            {
                importedScene.indices.emplace_back(mesh->mFaces[i].mIndices[j]);
            }
        }
        meshEntry.indexCount = importedScene.indices.size() - meshEntry.indexOffset;



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
                    CHECK(material->GetTexture(textureType, 0, &texturePath) == aiReturn_SUCCESS, "Mesh has no {} Texture at path: {}", aiTextureTypeToString(textureType), texturePath.C_Str());

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
                return {};
            };

            meshEntry.textures.albedo = loadTextureOfType(aiTextureType_DIFFUSE);
            meshEntry.textures.normal = loadTextureOfType(aiTextureType_NORMALS);
            meshEntry.textures.metallicRoughness = loadTextureOfType(aiTextureType_GLTF_METALLIC_ROUGHNESS);
        }
    }

    aiReleaseImport(scene);
    return importedScene;
}