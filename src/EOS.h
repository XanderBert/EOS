#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "window.h"
#include "defines.h"
#include "enums.h"
#include "handle.h"
#include "utils.h"


namespace EOS
{
    class ShaderCompiler;
    //TODO: Almost everything here will need Doxygen documentation, as the end user will work with these functions
    //I would also like to return my own type of VkResult on functions for the end user.

    /**
    * @brief Concept for the required HandleType operations.
    */
    template<typename T>
    concept ValidHolder = requires(T t)
    {
        { t.Valid() } -> std::convertible_to<bool>;
        { t.Empty() } -> std::convertible_to<bool>;
        { t.Gen() } -> std::convertible_to<uint32_t>;
        { t.Index() } -> std::convertible_to<uint32_t>;
        { t.IndexAsVoid() } -> std::convertible_to<void*>;
    };

    //Forward declare
    class IContext;

    template<typename HandleType>
    requires ValidHolder<HandleType>
    class Holder;

    //Create our Handle structures
    using ComputePipelineHandle     = Handle<struct ComputePipeline>;
    using RenderPipelineHandle      = Handle<struct RenderPipeline>;
    using RayTracingPipelineHandle  = Handle<struct RayTracingPipeline>;
    using ShaderModuleHandle        = Handle<struct ShaderModule>;
    using SamplerHandle             = Handle<struct Sampler>;
    using BufferHandle              = Handle<struct Buffer>;
    using TextureHandle             = Handle<struct Texture>;
    using QueryPoolHandle           = Handle<struct QueryPool>;
    using AccelStructHandle         = Handle<struct AccelerationStructure>;

    struct HardwareDeviceDescription final
    {
        uintptr_t ID{};
        HardwareDeviceType Type { HardwareDeviceType::Integrated} ;
        std::string Name{};
    };

    struct ContextConfiguration final
    {
        bool EnableValidationLayers{ true };
        ColorSpace DesiredSwapChainColorSpace { ColorSpace::SRGB_Linear };
    };

    struct ContextCreationDescription final
    {
        ContextConfiguration    Config;
        HardwareDeviceType      PreferredHardwareType{HardwareDeviceType::Discrete};
        const char*             ApplicationName{};
        void*                   Window{};
        void*                   Display{};
        int                     Width{};
        int                     Height{};
    };

    /**
     * @brief Used for buffer StateTransitioning without changing the index queue
     */
    struct GlobalBarrier final
    {
        const BufferHandle      Buffer;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

    struct ImageBarrier final
    {
        const TextureHandle    Texture;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

    struct ShaderInfo final
    {
        std::vector<uint32_t> Spirv;
        EOS::ShaderStage ShaderStage;
        uint32_t PushConstantSize;
        const char* DebugName;
    };

    struct VertexInputData final
    {
        static constexpr uint32_t MAX_ATTRIBUTES = 16;
        static constexpr uint32_t MAX_BUFFERS = 16;

        struct VertexAttribute final
        {
            uint32_t Location = 0; // a buffer which contains this attribute stream
            uint32_t Binding = 0;
            VertexFormat Format = VertexFormat::Invalid; // per-element format
            uintptr_t Offset = 0; // an offset where the first element of this attribute stream starts
        } Attributes[MAX_ATTRIBUTES];

        struct VertexInputBinding final
        {
            uint32_t Stride = 0;
        } InputBindings[MAX_BUFFERS];


        uint32_t GetNumAttributes() const;
        uint32_t GetNumInputBindings() const;
        uint32_t GetVertexSize() const;

        //TODO: This is c-style
        bool operator==(const VertexInputData& other) const
        {
            return memcmp(this, &other, sizeof(VertexInputData)) == 0;
        }
    };

    struct SpecializationConstantEntry final
    {
        uint32_t ID = 0;
        uint32_t Offset = 0; // offset within SpecializationConstantDescription::Data
        size_t Size = 0;
    };

    struct SpecializationConstantDescription final
    {
        static constexpr uint8_t MaxSecializationConstants = 16;
        SpecializationConstantEntry Entries[MaxSecializationConstants] = {};

        const void* Data = nullptr;
        size_t DataSize = 0;

        uint32_t GetNumberOfSpecializationConstants() const;
    };

    struct ColorAttachment final
    {
        Format ColorFormat = Format::Invalid;
        bool BlendEnabled = false;

        BlendOp RGBBlendOp = BlendOp::Add;
        BlendOp AlphaBlendOp = BlendOp::Add;

        BlendFactor SrcRGBBlendFactor = BlendFactor::One;
        BlendFactor SrcAlphaBlendFactor = BlendFactor::One;
        BlendFactor DstRGBBlendFactor = BlendFactor::Zero;
        BlendFactor DstAlphaBlendFactor = BlendFactor::Zero;
    };

    struct StencilState final
    {
        StencilOp StencilFailureOp = StencilOp::Keep;
        StencilOp DepthFailureOp = StencilOp::Keep;
        StencilOp DepthStencilPassOp = StencilOp::Keep;
        CompareOp StencilCompareOp = CompareOp::AlwaysPass;

        uint32_t ReadMask = 0;
        uint32_t WriteMask = 0;
    };

    struct DepthState final
    {
        CompareOp CompareOpState = CompareOp::AlwaysPass;
        bool IsDepthWriteEnabled = false;
    };

    struct RenderPipelineDescription final
    {
        Topology PipelineTopology = Topology::Triangle;
        VertexInputData VertexInput;

        ShaderModuleHandle VertexShader;
        ShaderModuleHandle TessellationControlShader;
        ShaderModuleHandle TesselationShader;
        ShaderModuleHandle GeometryShader;
        ShaderModuleHandle TaskShader;
        ShaderModuleHandle MeshShader;
        ShaderModuleHandle FragmentShader;

        SpecializationConstantDescription SpecInfo = {};

        const char* EntryPointVert = "main";
        const char* EntryPointTesc = "main";
        const char* EntryPointTese = "main";
        const char* EntryPointGeom = "main";
        const char* EntryPointTask = "main";
        const char* EntryPointMesh = "main";
        const char* EntryPointFrag = "main";

        ColorAttachment ColorAttachments[EOS_MAX_COLOR_ATTACHMENTS] = {};
        Format DepthFormat = Format::Invalid;
        Format StencilFormat = Format::Invalid;

        CullMode PipelineCullMode = CullMode::Back;
        WindingMode FrontFaceWinding = WindingMode::CounterClockWise;
        PolygonMode PolygonModeDescription = PolygonMode::Fill;

        StencilState BackFaceStencil = {};
        StencilState FrontFaceStencil = {};

        uint32_t SamplesCount = 1;
        uint32_t PatchControlPoints = 0;
        float MinSampleShading = 0.0f;

        const char* DebugName = "";

        uint32_t GetNumColorAttachments() const;
    };

    struct RenderPass final
    {
        struct AttachmentDesc final
        {
            LoadOp LoadOpState = LoadOp::Invalid;
            StoreOp StoreOpState = StoreOp::Store;
            uint8_t Layer = 0;
            uint8_t Level = 0;
            float ClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float ClearDepth = 1.0f;
            uint32_t ClearStencil = 0;
        };

        AttachmentDesc Color[EOS_MAX_COLOR_ATTACHMENTS]{};
        AttachmentDesc Depth = {.LoadOpState = LoadOp::DontCare, .StoreOpState = StoreOp::DontCare};
        AttachmentDesc Stencil = {.LoadOpState = LoadOp::Invalid, .StoreOpState = StoreOp::DontCare};

        uint32_t GetNumColorAttachments() const;
    };

    struct Framebuffer final
    {
        struct AttachmentDesc final
        {
            TextureHandle Texture;
            TextureHandle ResolveTexture;
        };

        AttachmentDesc Color[EOS_MAX_COLOR_ATTACHMENTS]{};
        AttachmentDesc DepthStencil{};
        const char* DebugName = "";

        uint32_t GetNumColorAttachments() const;
    };

    struct Dependencies final
    {
        constexpr static  uint8_t MaxSubmitDependencies = 4;
        TextureHandle Textures[MaxSubmitDependencies]{};
        BufferHandle Buffers[MaxSubmitDependencies]{};
    };

    struct Viewport final
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 1.0f;
        float Height = 1.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    struct BufferDescription final
    {
        BufferUsageFlags Usage = BufferUsageFlags::None;
        StorageType Storage = StorageType::HostVisible;
        size_t Size = 0;
        const void* Data = nullptr;
        const char* DebugName = "";
    };

#pragma region INTERFACES
    //TODO: instead of interfaces use concept and a forward declare. And then every API implements 1 class of that name with the concept.
    //CMake should handle that only 1 type of API is being used at the time.
    //This way we can completely get rid of inheritance

    class ICommandBuffer
    {
    public:
        DELETE_COPY_MOVE(ICommandBuffer);
        virtual ~ICommandBuffer() = default;

    protected:
        ICommandBuffer() = default;
    };

    class IContext
    {
    public:
        DELETE_COPY_MOVE(IContext);
        virtual ~IContext() = default;

        /**
        * @brief Fetches a free commandbuffer, one gets freed if needed (this can be blocking if none are free) and starts recording.
        * @return A free commandbuffer that is already recording.
        */
        virtual ICommandBuffer& AcquireCommandBuffer() = 0;

        /**
        * @brief Submits the commandbuffer, Presents the swapchain if desired and processes all tasks that have been defered until after submition (like resource destruction).
        * @param commandBuffer The commandbuffer we want to submit to the GPU.
        * @param present A swapchain texture where it should be presented to.
        * @return A Handle for this submission.
        */
        virtual SubmitHandle Submit(ICommandBuffer& commandBuffer, TextureHandle present) = 0;

        /**
         * @brief Gets the handle to the currently in use SwapChain.
         * @return The handle of the currently in use SwapChain.
         */
        virtual TextureHandle GetSwapChainTexture() = 0;

        /**
        * @brief Gets the format to the currently in use SwapChain.
        * @return The format of the currently in use SwapChain.
        */
        virtual Format GetSwapchainFormat() const = 0;

        /**
        * @brief Creates shader module from a compiled shader.
        * @param shaderInfo information about the shader such as its code and stage.
        * @return A Holder Handle to a shader module.
        */
        virtual EOS::Holder<EOS::ShaderModuleHandle> CreateShaderModule(const EOS::ShaderInfo& shaderInfo) = 0;

        /**
        * @brief Creates a RenderPipeline and returns a handle to it.
        * @param renderPipelineDescription The description about what type of pipeline we want to create and what it exists of.
        * @return A Holder Handle to a Render Pipeline.
        */
        virtual EOS::Holder<EOS::RenderPipelineHandle> CreateRenderPipeline(const RenderPipelineDescription& renderPipelineDescription) = 0;


        /**
        * @brief Creates a Buffer and returns a handle to it.
        * @param description desecribes the what sort of buffer it is and its properties.
        * @return A Holder Handle to the buffer.
        */
        virtual EOS::Holder<BufferHandle> CreateBuffer(const BufferDescription& description) = 0;

        /**
        * @brief Handles the destruction of a TextureHandle and what it holds.
        * @param handle The handle to the texture you want to destroy.
        */
        virtual void Destroy(TextureHandle handle) = 0;


        /**
        * @brief Handles the destruction of a ShaderModuleHandle and what it holds.
        * @param handle The handle to the shaderModule you want to destroy.
        */
        virtual void Destroy(ShaderModuleHandle handle) = 0;

        /**
        * @brief Handles the destruction of a RenderPipelineHandle and what it holds.
        * @param handle The handle to the Renderpipeline you want to destroy.
        */
        virtual void Destroy(RenderPipelineHandle handle) = 0;

        /**
         * @brief Handles the destruction of a BufferHandle and what it holds.
         * @param handle The handle to the Buffer you want to destroy.
         */
        virtual void Destroy(BufferHandle handle) = 0;

        /**
         * @brief
         * @param handle The handle of the buffer we want to upload to.
         * @param data The data we want to upload.
         * @param size The size of the data we want to upload.
         * @param offset The offset it needs to have inside of the buffer.
         */
        virtual void Upload(EOS::BufferHandle handle, const void* data, size_t size, size_t offset) = 0;

    protected:
        IContext() = default;
    };
#pragma endregion

    template<typename HandleType>
    requires ValidHolder<HandleType>
    class Holder final
    {
    public:
        Holder() = default;
        Holder(EOS::IContext* context, HandleType handle) : HolderContext(context), Handle(handle) {}

        ~Holder()
        {
            CHECK(HolderContext, "the context of the holder is no longer valid in the destruction of the holder");
            if (HolderContext)
            {
                HolderContext->Destroy(Handle);
            }
        }

        DELETE_COPY(Holder);

        Holder(Holder&& other) noexcept : HolderContext(other.HolderContext), Handle(other.Handle)
        {
            other.HolderContext = nullptr;
            other.Handle = HandleType{};
        }

        Holder& operator=(Holder&& other) noexcept
        {
            std::swap(HolderContext, other.HolderContext);
            std::swap(Handle, other.Handle);
            return *this;
        }

        Holder& operator=(std::nullptr_t)
        {
            this->Reset();
            return *this;
        }

        operator HandleType() const
        {
            return Handle;
        }

        bool Valid() const
        {
            return Handle.Valid();
        }

        bool Empty() const
        {
            return Handle.Empty();
        }

        void Reset()
        {
            CHECK(HolderContext, "the context of the holder is no longer valid while resetting the holder");
            if (HolderContext)
            {
                HolderContext->Destroy(Handle);
            }

            HolderContext = nullptr;
            Handle = HandleType{};
        }

        HandleType Release()
        {
            HolderContext = nullptr;
            return std::exchange(Handle, HandleType{});
        }

        uint32_t Gen() const
        {
            return Handle.Gen();
        }

        uint32_t Index() const
        {
            return Handle.Index();
        }

        void* IndexAsVoid() const
        {
            return Handle.IndexAsVoid();
        }

    private:
        EOS::IContext* HolderContext = nullptr;
        HandleType Handle = {};
    };
    static_assert(sizeof(Holder<Handle<class Foo>>) == sizeof(uint64_t) + PTR_SIZE);


    /**
    * @brief Creates a context for the used Graphics API and after that it creates a Swapchain for it.
    * @param contextCreationDescription The settings with which we want to create our Context.
    * @returns A unique pointer to the created Context interface.
    */
    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription);

    /**
    * @brief Creates a ShaderCompiler.
    * @param shaderFolder The folder where our non-compiled shaders are stored.
    * @returns A unique pointer to the created shader compiler.
    */
    std::unique_ptr<ShaderCompiler> CreateShaderCompiler(const std::filesystem::path& shaderFolder);
}

#pragma region GLOBAL_FUNCTIONS

/**
* @brief Inserts a pipeline barrier in the commandbuffer.
* @param commandBuffer The commandbuffer we want to insert the barrier into.
* @param globalBarriers The globalBarriers we want to insert used for Buffer barriers.
* @param imageBarriers The imageBarriers we want to insert
*/
void cmdPipelineBarrier(const EOS::ICommandBuffer& commandBuffer, const std::vector<EOS::GlobalBarrier>& globalBarriers, const std::vector<EOS::ImageBarrier>& imageBarriers);


/**
 * @brief Add a command to the commandbuffer that we will now start rendering, defining what should be rendered and what dependencies we have.
 * @param commandBuffer The commandbuffer where we add the command to.
 * @param renderPass Describes what how our framebuffer attachements should be loaded / stored ...
 * @param description Describes our actual textures we want to use for rendering.
 * @param dependancies Describes the depandancies of this "Pass".
 */
void cmdBeginRendering(EOS::ICommandBuffer& commandBuffer, const EOS::RenderPass& renderPass, EOS::Framebuffer& description, const EOS::Dependencies& dependancies = {});

/**
 * @brief Add a command to the commandbuffer that we will now end rendering
 * @param commandBuffer The commandbuffer we want to record into
 */
void cmdEndRendering(EOS::ICommandBuffer& commandBuffer);

/**
 * @brief Records the command to bind the specified graphics pipeline.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param renderPipelineHandle The handle to the pipeline we want to bind.
 */
void cmdBindRenderPipeline(EOS::ICommandBuffer& commandBuffer, EOS::RenderPipelineHandle renderPipelineHandle);

/**
 * @brief Binds the commandBuffer
 * @param commandBuffer The commandBuffer to send the command to to bind the VertexBuffer.
 * @param index The Index of the vertexBuffer.
 * @param buffer The Handle to the buffer.
 * @param bufferOffset The offset the buffer has.
 */
void cmdBindVertexBuffer(const EOS::ICommandBuffer& commandBuffer, uint32_t index, EOS::BufferHandle buffer, uint64_t bufferOffset = 0);


/**
 * @brief Binds the indexBuffer
 * @param commandBuffer The commandBuffer to send the command to to bind the indexbuffer.
 * @param indexBuffer The indexBuffer.
 * @param indexFormat The int format of the index buffer (speifies how many bytes 1 index is).
 * @param indexBufferOffset The offset of the indexBuffer
 */
void cmdBindIndexBuffer(const EOS::ICommandBuffer& commandBuffer, EOS::BufferHandle indexBuffer, EOS::IndexFormat indexFormat, uint64_t indexBufferOffset = 0);

/**
 * @brief Records a simple draw command into the specified commandbuffer.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param vertexCount The amount of vertices we want to record.
 * @param instanceCount The amount of instances to draw.
 * @param firstVertex The index of the first vertex to draw.
 * @param baseInstance The instance ID of the first instance to draw.
 */
void cmdDraw(const EOS::ICommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t baseInstance = 0);

/**
 * @brief Records a indexed draw command into the specified commandbuffer.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param indexCount The amount of indices we want to draw.
 * @param instanceCount The amount of instances we want to draw.
 * @param firstIndex At what index the draw command should start to draw.
 * @param vertexOffset At what offset we should start usng the vertices.
 * @param baseInstance At what Instance we want to start drawing.
 */
void cmdDrawIndexed(const EOS::ICommandBuffer& commandBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t baseInstance = 0);

/**
 * @brief Binds push constants.
 * @param commandBuffer The commandbuffer we want to record to to bind our push constants.
 * @param data The actual data we want to bind.
 * @param size The size of the data we want to bind.
 * @param offset At what offset we would like to start to bind the data from.
 */
void cmdPushConstants(const EOS::ICommandBuffer& commandBuffer, const void* data, size_t size, size_t offset = 0);

/**
 * @brief Templated helper function to bind push constants.
 * @tparam Struct The structure we would like to bind as push constants.
 * @param commandBuffer The commandbuffer we want to record to to bind our push constants.
 * @param data The data structure we want to bind as pushconstants
 * @param offset At what offset we would like to start to bind the data from.
 */
template<typename Struct>
void cmdPushConstants(const EOS::ICommandBuffer& commandBuffer, const Struct& data, size_t offset = 0)
{
    cmdPushConstants(commandBuffer, &data, sizeof(Struct), offset);
}

/**
 * @brief Adds a debug marker that is visible in debug software.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param label The name of the marker.
 * @param colorRGBA The color of the marker.
 */
void cmdPushMarker(const EOS::ICommandBuffer& commandBuffer, const char* label, uint32_t colorRGBA);

/**
 * @brief Pops the last set debug marker.
 * @param commandBuffer  The commandbuffer we want to record into.
 */
void cmdPopMarker(const EOS::ICommandBuffer& commandBuffer);



#pragma endregion