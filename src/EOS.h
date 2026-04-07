#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "window.h"
#include "defines.h"
#include "enums.h"
#include "handle.h"

#if defined(EOS_USE_TRACY)
#include <tracy/Tracy.hpp>

#define EOS_PROFILER_FUNCTION() ZoneScoped
#define EOS_PROFILER_FUNCTION_COLOR(color) ZoneScopedC(color)
#define EOS_PROFILER_FRAME(name) FrameMarkNamed(name)
#define EOS_PROFILER_COLOR_WAIT 0xff0000
#define EOS_PROFILER_COLOR_SUBMIT 0x0000ff
#define EOS_PROFILER_COLOR_PRESENT 0x00ff00
#define EOS_PROFILER_COLOR_CREATE 0xff6600
#define EOS_PROFILER_COLOR_DESTROY 0xffa500
#define EOS_PROFILER_COLOR_BARRIER 0xffffff
#define EOS_PROFILER_COLOR_CMD_DRAW 0x8b0000
#define EOS_PROFILER_COLOR_CMD_COPY 0x8b0a50
#define EOS_PROFILER_COLOR_CMD_RTX 0x8b0000
#define EOS_PROFILER_COLOR_CMD_DISPATCH 0x8b0000
#else
#define EOS_PROFILER_FUNCTION()
#define EOS_PROFILER_FUNCTION_COLOR(color)
#define EOS_PROFILER_FRAME(name)
#endif

namespace EOS
{
    class ShaderCompiler;

    /**
    * @brief Concept for the required HandleType operations.
    */    
    template<typename T>
    concept ValidHolder = requires(T t)
    {
        { t.Valid() } -> std::convertible_to<bool>;
        { t.Empty() } -> std::convertible_to<bool>;
        { t.Gen() } -> std::convertible_to<uint32_t>;        { t.Index() } -> std::convertible_to<uint32_t>;
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

    using ComputePipelineHolder     = Holder<ComputePipelineHandle>;
    using RenderPipelineHolder      = Holder<RenderPipelineHandle>;
    using RayTracingPipelineHolder  = Holder<RayTracingPipelineHandle>;
    using ShaderModuleHolder        = Holder<ShaderModuleHandle>;
    using SamplerHolder             = Holder<SamplerHandle>;
    using BufferHolder              = Holder<BufferHandle>;
    using TextureHolder             = Holder<TextureHandle>;
    using QueryPoolHolder           = Holder<QueryPoolHandle>;
    using AccelStructHolder         = Holder<AccelStructHandle>;


    /**
     * @brief Describes a physical GPU that can be selected for context creation.
     */
    struct HardwareDeviceDescription final
    {
        uintptr_t ID{};
        HardwareDeviceType Type { HardwareDeviceType::Integrated} ;
        std::string Name{};
    };

    /**
     * @brief Global context configuration options.
     */
    struct ContextConfiguration final
    {
        bool EnableValidationLayers{ true };
        ColorSpace DesiredSwapChainColorSpace { ColorSpace::SRGB_Linear };
    };

    /**
     * @brief Input settings used when creating the graphics context and swapchain.
     */
    struct ContextCreationDescription final
    {
        ContextConfiguration    Config;
        HardwareDeviceType      PreferredHardwareType{HardwareDeviceType::Discrete};

#if defined(EOS_PROJECT_SHADER_PATH)
        std::filesystem::path   ShaderPath{EOS_PROJECT_SHADER_PATH};
#else
        std::filesystem::path   ShaderPath{};
#endif

#if defined(EOS_ENGINE_SHADER_PATH)
        std::filesystem::path   EngineShaderPath{EOS_ENGINE_SHADER_PATH};
#else
        std::filesystem::path   EngineShaderPath{"../src/shaders"};
#endif

#if defined(EOS_SHADER_OUTPUT_PATH)
        std::filesystem::path   ShaderOutputPath{EOS_SHADER_OUTPUT_PATH};
#else
        std::filesystem::path   ShaderOutputPath{"./"};
#endif

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

    /**
     * @brief Used for texture state transitioning without changing queue ownership.
     */
    struct ImageBarrier final
    {
        const TextureHandle    Texture;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

    /**
     * @brief Compiled shader payload and metadata returned by the shader compiler.
     */
    struct ShaderInfo final
    {
        std::vector<uint32_t> Spirv;
        EOS::ShaderStage ShaderStage;
        uint32_t PushConstantSize;
        std::string DebugName;
    };

    /**
     * @brief Vertex layout description used for graphics pipeline creation.
     */
    struct VertexInputData final
    {
        static constexpr uint32_t MAX_ATTRIBUTES = 16;
        static constexpr uint32_t MAX_BUFFERS = 16;

        /**
         * @brief Single vertex attribute mapping.
         */
        struct VertexAttribute final
        {
            uint32_t Location = 0; // a buffer which contains this attribute stream
            uint32_t Binding = 0;
            VertexFormat Format = VertexFormat::Invalid; // per-element format
            uintptr_t Offset = 0; // an offset where the first element of this attribute stream starts
        } Attributes[MAX_ATTRIBUTES];

        /**
         * @brief Vertex buffer binding description.
         */
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

    /**
     * @brief One specialization constant mapping entry.
     */
    struct SpecializationConstantEntry final
    {
        uint32_t ID = 0;
        uint32_t Offset = 0; // offset within SpecializationConstantDescription::Data
        size_t Size = 0;
    };

    /**
     * @brief Specialization constant block for pipeline creation.
     */
    struct SpecializationConstantDescription final
    {
        static constexpr uint8_t MaxSecializationConstants = 16;
        SpecializationConstantEntry Entries[MaxSecializationConstants] = {};

        const void* Data = nullptr;
        size_t DataSize = 0;

        uint32_t GetNumberOfSpecializationConstants() const;
    };

    /**
     * @brief Blend and format configuration for a single color attachment.
     */
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

    /**
     * @brief Stencil operations for one stencil face.
     */
    struct StencilState final
    {
        StencilOp StencilFailureOp = StencilOp::Keep;
        StencilOp DepthFailureOp = StencilOp::Keep;
        StencilOp DepthStencilPassOp = StencilOp::Keep;
        CompareOp StencilCompareOp = CompareOp::AlwaysPass;

        uint32_t ReadMask = 0;
        uint32_t WriteMask = 0;
    };

    /**
     * @brief Depth test and depth-write configuration.
     */
    struct DepthState final
    {
        CompareOp CompareOpState = CompareOp::AlwaysPass;
        bool IsDepthWriteEnabled = false;
    };

    /**
     * @brief Full graphics pipeline creation description.
     */
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

        bool DepthClamping = false;

        const char* DebugName = "";

        uint32_t GetNumColorAttachments() const;
    };

    /**
   * @brief Compute pipeline creation description.
   */
    struct ComputePipelineDescription final
    {
        ShaderModuleHandle ComputeShader;
        SpecializationConstantDescription SpecInfo{};
        const char* EntryPoint = "main";
        const char* DebugName = "";
    };

    /**
     * @brief Attachment load/store behavior for a render pass.
     */
    struct RenderPass final
    {
        /**
         * @brief Per-attachment load/store state and clear values.
         */
        struct AttachmentDesc final
        {
            LoadOp LoadOpState = LoadOp::Invalid;
            StoreOp StoreOpState = StoreOp::Store;
            uint8_t Layer = 0;
            uint8_t LayerCount = 1;
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

    /**
     * @brief Texture attachments bound for rendering.
     */
    struct Framebuffer final
    {
        /**
         * @brief Color or depth attachment pair (with optional resolve target).
         */
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

    /**
     * @brief Resource dependency list used when starting a render pass.
     */
    struct Dependencies final
    {
        constexpr static  uint8_t MaxSubmitDependencies = 4;
        TextureHandle Textures[MaxSubmitDependencies]{};
        BufferHandle Buffers[MaxSubmitDependencies]{};
    };

    /**
     * @brief Rectangle region used for scissor testing.
     */
    struct ScissorRect final
    {
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    /**
     * @brief Viewport transform description.
     */
    struct Viewport final
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 1.0f;
        float Height = 1.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    /**
     * @brief Buffer creation and optional initialization data.
     */
    struct BufferDescription final
    {
        BufferUsageFlags Usage = BufferUsageFlags::None;
        StorageType Storage = StorageType::HostVisible;
        size_t Size = 0;
        const void* Data = nullptr;
        const char* DebugName = "";
    };

    /**
     * @brief Component swizzle mapping used by texture views/samplers.
     */
    struct ComponentMapping final
    {
        Swizzle R = Swizzle::Identity;
        Swizzle G = Swizzle::Identity;
        Swizzle B = Swizzle::Identity;
        Swizzle A = Swizzle::Identity;

        bool Identity() const
        {
            return R == Swizzle::Identity && G == Swizzle::Identity && B == Swizzle::Identity && A == Swizzle::Identity;
        }
    };

    /**
     * @brief 3D extent helper for textures and regions.
     */
    struct Dimensions final
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Depth = 1;

        inline Dimensions divide1D(uint32_t v) const
        {
            return {.Width = Width / v, .Height = Height, .Depth = Depth};
        }

        inline Dimensions divide2D(uint32_t v) const
        {
            return {.Width = Width / v, .Height = Height / v, .Depth = Depth};
        }

        inline Dimensions divide3D(uint32_t v) const
        {
            return {.Width = Width / v, .Height = Height / v, .Depth = Depth / v};
        }

        inline bool operator==(const Dimensions& other) const
        {
            return Width == other.Width && Height == other.Height && Depth == other.Depth;
        }
    };

    /**
     * @brief Texture creation and upload settings.
     */
    struct TextureDescription final
    {
        EOS::ImageType Type = EOS::ImageType::Image_2D;
        Format TextureFormat = Format::Invalid;
        Dimensions TextureDimensions = {1, 1, 1};
        uint32_t NumberOfLayers = 1;
        uint32_t NumberOfMipLevels = 1;
        uint32_t NumberOfSamples = 1;
        uint8_t Usage = TextureUsageFlags::Sampled;
        StorageType Storage = StorageType::Device;
        ComponentMapping Swizzle{};
        const void* Data = nullptr;
        uint32_t DataNumberOfMipLevels = 1;     // how many mip-levels we want to upload
        bool GenerateMipmaps = false;           // generate mip-levels immediately, valid only with non-null data
        const char* DebugName = "";
    };

    /**
     * @brief Integer 3D offset used in texture copy/update ranges.
     */
    struct Offset3D final
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Z = 0;
    };

    /**
     * @brief Subresource range used for texture uploads.
     */
    struct TextureRangeDescription final
    {
        Offset3D Offset = {};
        Dimensions Dimension = {1, 1, 1};
        uint32_t Layer = 0;
        uint32_t NumberOfLayers = 1;
        uint32_t MipLevel = 0;
        uint32_t NumberOfMipLevels = 1;
    };

    /**
     * @brief Helper description for loading textures from disk.
     */
    struct TextureLoadingDescription final
    {
        std::filesystem::path InputFilePath;
        std::filesystem::path OutputFilePath;
        Compression TextureCompression;
        uint8_t Usage = Sampled;
        ImageType Type = ImageType::Image_2D;
        IContext* Context;
    };

    /**
     * @brief Sampler creation settings.
     */
    struct SamplerDescription final
    {
        SamplerFilter minFilter = LinearFilter;
        SamplerFilter magFilter = LinearFilter;
        SamplerMip mipMap = Disabled;
        SamplerWrap wrapU = Repeat;
        SamplerWrap wrapV = Repeat;
        SamplerWrap wrapW = Repeat;
        CompareOp depthCompareOp = CompareOp::LessEqual;
        uint8_t mipLodMin = 0;
        uint8_t mipLodMax = 15;
        uint8_t maxAnisotropic = 1;
        bool depthCompareEnabled = false;
        const char* debugName = "";
    };

    struct AccelerationStructBuildRange final
    {
        uint32_t PrimitiveCount = 0;
        uint32_t PrimitiveOffset = 0;
        uint32_t FirstVertex = 0;
        uint32_t TransformOffset = 0;
    };

    struct AccelStructInstance final
    {
        float Transform[3][4];
        uint32_t InstanceCustomIndex : 24 = 0;
        uint32_t Mask : 8 = 0xff;
        uint32_t InstanceShaderBindingTableRecordOffset : 24 = 0;
        uint32_t Flags : 8 = TriangleFacingCullDisable;
        uint64_t AccelerationStructureReference = 0;
    };

    struct AccelerationStructDescription final
    {
        AccelerationStructureType Type                  = InvalidAccelerationStructure;
        AccelerationStructureGeometryType GeometryType  = Triangles;
        uint8_t GeometryFlags                           = Opaque;

        VertexFormat VertexFormatStructure              = EOS::VertexFormat::Invalid;
        BufferHandle VertexBuffer;
        uint32_t VertexStride                           = 0; // zero means the size of `vertexFormat`
        uint32_t NumberOfVertices                       = 0;
        BufferHandle IndexBuffer;
        BufferHandle TransformBuffer;
        BufferHandle InstancesBuffer;
        AccelerationStructBuildRange BuildRange        = {};
        uint8_t BuildFlags                              = PreferFastTrace;
        const char* DebugName                           = "";
    };

    /**
     * @brief Indirect indexed draw command layout.
     */
    struct DrawIndexedIndirectCommand final
    {
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        int32_t vertexOffset{};
        uint32_t firstInstance{};
    };

#pragma region INTERFACES
    //TODO: instead of interfaces use concept and a forward declare. And then every API implements 1 class of that name with the concept.
    //CMake should handle that only 1 type of API is being used at the time.
    //This way we can completely get rid of inheritance

    /**
     * @brief Opaque command buffer interface used by command recording helpers.
     */
    class ICommandBuffer
    {
    public:
        DELETE_COPY_MOVE(ICommandBuffer);
        virtual ~ICommandBuffer() = default;

    protected:
        ICommandBuffer() = default;
    };

    /**
     * @brief Main graphics context interface for resource creation and submission.
     */
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
         * @brief Gets the handle to the currently in use SwapChain image and advances to the
         *        next one. Internally this CPU-waits (via the timeline semaphore) until the
         *        previous frame's GPU work for this swapchain slot is fully complete.
         *
         * @note  **Call this BEFORE uploading any per-frame GPU buffers** (e.g. via Upload()).
         *        The wait that happens inside this call is what makes it safe to overwrite
         *        those buffers. Uploading before this call creates a data race: the previous
         *        frame's GPU commands may still be reading the buffer while the CPU writes
         *        the new frame's data into it, causing one-frame-stale values to be used
         *        on the GPU
         *
         *        Correct per-frame pattern:
         *        @code
         *        ICommandBuffer& cmd = AcquireCommandBuffer();
         *        TextureHandle swapchain = GetSwapChainTexture(); // <-- wait happens here
         *        Upload(myPerFrameBuffer, data, size, 0);         // <-- now safe to write
         *        cmdPipelineBarrier(cmd, {}, {{ swapchain, ... }});
         *        @endcode
         *
         * @return The handle of the currently in use SwapChain image.
         */
        virtual TextureHandle GetSwapChainTexture() = 0;

        /**
        * @brief Gets the format to the currently in use SwapChain.
        * @return The format of the currently in use SwapChain.
        */
        virtual Format GetSwapchainFormat() const = 0;

        /**
         * @brief Gets the color space of the currently active swapchain image.
         * @return The color space of the currently in use SwapChain.
         */
        virtual ColorSpace GetSwapchainColorSpace() const = 0;

        /**
         * @brief Gets dimensions for a texture handle.
         * @param handle The handle of the texture to query.
         * @return The dimensions of the texture.
         */
        virtual Dimensions GetDimensions(TextureHandle handle) const = 0;

        /**
        * @brief Creates shader module from a compiled shader.
        * @param fileName The name of the shader.
        * @param shaderStage The stage of the shader
        * @return A Holder to a shader module.
        */
        virtual EOS::Holder<EOS::ShaderModuleHandle> CreateShaderModule(const char* fileName, ShaderStage shaderStage) = 0;

        /**
        * @brief Creates a RenderPipeline and returns a handle to it.
        * @param renderPipelineDescription The description about what type of pipeline we want to create and what it exists of.
        * @return A Holder Handle to a Render Pipeline.
        */
        virtual EOS::Holder<EOS::RenderPipelineHandle> CreateRenderPipeline(const RenderPipelineDescription& renderPipelineDescription) = 0;

        /**
        * @brief Creates a ComputePipeline and returns a handle to it.
        * @param computePipelineDescription The description about the pipeline we want to create.
        * @return A Holder Handle to a Compute Pipeline.
        */
        virtual EOS::Holder<EOS::ComputePipelineHandle> CreateComputePipeline(const ComputePipelineDescription& computePipelineDescription) = 0;

        /**
        * @brief Reloads changed shader files and rebuilds affected pipelines.
        * @return Number of pipelines rebuilt.
        */
        virtual uint32_t ReloadShaders() = 0;


        /**
        * @brief Creates a Buffer and returns a handle to it.
        * @param description describes what sort of buffer it is and its properties.
        * @return A Holder Handle to the buffer.
        */
        virtual EOS::Holder<BufferHandle> CreateBuffer(const BufferDescription& description) = 0;

        /**
         * @brief Creates a texture and uploads its data to the GPU if needed.
         * @param description describes what type of texture it is.
         * @return A Holder to the texture.
         */
        virtual EOS::Holder<TextureHandle> CreateTexture(const TextureDescription& description) = 0;


        /**
         * @brief Creates a sampler.
         * @param samplerDescription description of the sampler
         * @return A holder to the sampler
         */
        virtual EOS::Holder<EOS::SamplerHandle> CreateSampler(const EOS::SamplerDescription& samplerDescription) = 0;

        /**
         * @brief Creates a Acceleration Structure.
         * @param desc description of the Acceleration Structure
         * @return A holder to the Acceleration Structure
         */
        virtual EOS::Holder<AccelStructHandle> CreateAccelerationStructure(const AccelerationStructDescription& desc) = 0;


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
        * @brief Handles the destruction of a ComputePipeline and what it holds.
        * @param handle The handle to the ComputePipeline you want to destroy.
        */
        virtual void Destroy(ComputePipelineHandle handle) = 0;

        /**
        * @brief Handles the destruction of a BufferHandle and what it holds.
        * @param handle The handle to the Buffer you want to destroy.
        */
        virtual void Destroy(BufferHandle handle) = 0;

        /**
         * @brief Handles the destruction of a SamplerHandle and what it holds.
         * @param handle The handle to the Sampler you want to destroy
         */
        virtual void Destroy(EOS::SamplerHandle handle) = 0;

        /**
        * @brief Handles the destruction of a AccelStructHandle and what it holds.
        * @param handle The handle to the AccelStructure you want to destroy
        */
        virtual void Destroy(EOS::AccelStructHandle handle) = 0;

        /**
        * @brief Handles the uploading of buffers to the GPU.
        * @param handle The handle of the buffer we want to upload to.
        * @param data The data we want to upload.
        * @param size The size of the data we want to upload.
        * @param offset The offset it needs to have inside the buffer.
        *
        * @warning For per-frame GPU buffers (e.g. uniform/storage buffers updated every frame),
        *          you MUST call GetSwapChainTexture() before calling this. GetSwapChainTexture()
        *          performs the CPU-side wait that ensures the previous frame's GPU commands have
        *          finished reading the buffer. Uploading before that wait is a data race.
        *          See GetSwapChainTexture() for the correct call order.
        */
        virtual void Upload(EOS::BufferHandle handle, const void* data, size_t size, size_t offset) = 0;

        /**
         * @brief Gets the GPU virtual address of a buffer region.
         * @param handle The handle of the buffer.
         * @param offset Byte offset inside the buffer.
         * @return GPU virtual address for the requested buffer location.
         */
        virtual uint64_t GetGPUAddress(BufferHandle handle, size_t offset = 0) const = 0;

        /**
         * @brief Gets the GPU virtual address of an acceleration structure.
         * @param handle The handle of the acceleration structure.
         * @return GPU virtual address for the acceleration structure.
         */
        virtual uint64_t GetGPUAddress(AccelStructHandle handle) const = 0;

        /**
         * @brief Gets the mapped CPU pointer for a host-visible buffer.
         * @param handle The handle of the mapped buffer.
         * @return CPU pointer to the mapped memory.
         */
        virtual uint8_t* GetMappedPtr(BufferHandle handle) const = 0;

        /**
         * @brief Flushes mapped host writes to make them visible to the GPU.
         * @param handle The handle of the mapped buffer.
         * @param size Number of bytes to flush.
         * @param offset Byte offset inside the buffer to start flushing from.
         */
        virtual void  FlushMappedMemory(BufferHandle handle, size_t size, size_t offset = 0) = 0;

        /**
        * @brief Handles the uploading of textures to the GPU
        * @param handle The handle of the texture we want to upload
        * @param range The description of how many mips, layers ... we want to upload.
        * @param data The actual data of the texture we want to upload.
        */
        virtual void Upload(EOS::TextureHandle handle, const TextureRangeDescription& range, const void* data) = 0;

        /**
         * @brief Gets the texture format for a texture handle.
         * @param handle The handle of the texture you want to get the format from.
         * @return Texture format of the referenced texture.
         */
        virtual Format GetFormat(TextureHandle handle) const = 0;

    protected:
        IContext() = default;
    };
#pragma endregion

    template<typename HandleType>
    requires ValidHolder<HandleType>
    /**
     * @brief RAII wrapper for EOS resource handles.
     *
     * Automatically destroys the owned resource through its originating context
     * when the holder goes out of scope, unless ownership was released.
     */
    class Holder final
    {
    public:
        /**
         * @brief Creates an empty holder.
         */
        Holder() = default;

        /**
         * @brief Creates a holder that owns an existing handle.
         * @param context Context that will be used for destruction.
         * @param handle Resource handle to own.
         */
        Holder(EOS::IContext* context, HandleType handle) : HolderContext(context), Handle(handle) {}

        ~Holder()
        {
            //TODO: CHECK(HolderContext, "the context of the holder is no longer valid in the destruction of the holder");
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

        /**
         * @brief Implicit conversion to the underlying handle type.
         * @return Owned handle value.
         */
        operator HandleType() const
        {
            return Handle;
        }

        /**
         * @brief Checks if the held handle is valid.
         * @return True if the held handle references a live resource.
         */
        bool Valid() const
        {
            return Handle.Valid();
        }

        /**
         * @brief Checks if the holder is empty.
         * @return True if no resource is currently owned.
         */
        bool Empty() const
        {
            return Handle.Empty();
        }

        /**
         * @brief Destroys the owned resource and resets this holder.
         */
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

        /**
         * @brief Releases ownership without destroying the resource.
         * @return The previously owned handle.
         */
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
 * @brief Records a viewport state bind command.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param viewport The viewport size we want to set.
 */
void cmdBindViewport(const EOS::ICommandBuffer& commandBuffer, const EOS::Viewport& viewport);

/**
 * @brief Records a scissor rectangle state bind command.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param scissor The scissor rectangle to bind.
 */
void cmdBindScissorRect(const EOS::ICommandBuffer& commandBuffer, const EOS::ScissorRect& scissor);


/**
 *
 * @param commandBuffer The commandbuffer we want to record into.
 * @param handle The handle to the ComputePipeline you want to bind.
 */
void cmdBindComputePipeline(EOS::ICommandBuffer& commandBuffer, EOS::ComputePipelineHandle handle);


/**
 *
 * @param commandBuffer The commandbuffer we want to record into.
 * @param threadGroupCount The 3D threadGroup Count to execute the compute pipeline.
 * @param dependencies The Input/Output dependencies of this pipeline
 */
void cmdDispatchThreadGroups(EOS::ICommandBuffer& commandBuffer, const EOS::Dimensions& threadGroupCount, const EOS::Dependencies& dependencies = {});

/**
 * @brief Add a command to the commandbuffer that we will now start rendering, defining what should be rendered and what dependencies we have.
 * @param commandBuffer The commandbuffer where we add the command to.
 * @param renderPass Describes what how our framebuffer attachements should be loaded / stored ...
 * @param description Describes our actual textures we want to use for rendering.
 * @param dependencies Describes the depandancies of this "Pass".
 */
void cmdBeginRendering(EOS::ICommandBuffer& commandBuffer, const EOS::RenderPass& renderPass, EOS::Framebuffer& description, const EOS::Dependencies& dependencies = {});

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
 * @param commandBuffer The commandBuffer to send the command to, to bind the VertexBuffer.
 * @param index The Index of the vertexBuffer.
 * @param buffer The Handle to the buffer.
 * @param bufferOffset The offset the buffer has.
 */
void cmdBindVertexBuffer(const EOS::ICommandBuffer& commandBuffer, uint32_t index, const EOS::BufferHandle& buffer, uint64_t bufferOffset = 0);


/**
 * @brief Binds the indexBuffer
 * @param commandBuffer The commandBuffer to send the command to, to bind the indexbuffer.
 * @param indexBuffer The indexBuffer.
 * @param indexFormat The int format of the index buffer (speifies how many bytes 1 index is).
 * @param indexBufferOffset The offset of the indexBuffer
 */
void cmdBindIndexBuffer(const EOS::ICommandBuffer& commandBuffer, const EOS::BufferHandle& indexBuffer, EOS::IndexFormat indexFormat, uint64_t indexBufferOffset = 0);

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
 * @brief Records an indexed draw command into the specified commandbuffer.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param indexCount The amount of indices we want to draw.
 * @param instanceCount The amount of instances we want to draw.
 * @param firstIndex At what index the draw command should start to draw.
 * @param vertexOffset At what offset we should start usng the vertices.
 * @param baseInstance At what Instance we want to start drawing.
 */
void cmdDrawIndexed(const EOS::ICommandBuffer& commandBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t baseInstance = 0);


/**
 * @brief Records an indexed indirect draw command.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param indirectBuffer The handle to the uploaded Indirect buffer containing the scene information.
 * @param indirectBufferOffset The offset inside the Indirect buffer.
 * @param drawCount How many meshes are in the scene.
 * @param stride Number of bytes between consecutive DrawIndexedIndirectCommand
              entries in the indirect buffer. Pass 0 to use the default
              packed stride (sizeof(DrawIndexedIndirectCommand)).
 */
void cmdDrawIndexedIndirect(const EOS::ICommandBuffer& commandBuffer, const EOS:: BufferHandle& indirectBuffer, size_t indirectBufferOffset, uint32_t drawCount, uint32_t stride = 0);

/**
 * @brief Binds push constants.
 * @param commandBuffer The commandbuffer we want to record to bind our push constants.
 * @param data The actual data we want to bind.
 * @param size The size of the data we want to bind.
 * @param offset At what offset we would like to start to bind the data from.
 */
void cmdPushConstants(const EOS::ICommandBuffer& commandBuffer, const void* data, size_t size, size_t offset = 0);

/**
 * @brief Templated helper function to bind push constants.
 * @tparam Struct The structure we would like to bind as push constants.
 * @param commandBuffer The commandbuffer we want to record to, to bind our push constants.
 * @param data The data structure we want to bind as pushconstants
 * @param offset At what offset we would like to start to bind the data from.
 */
template<typename Struct>
void cmdPushConstants(const EOS::ICommandBuffer& commandBuffer, const Struct& data, size_t offset = 0)
{
    cmdPushConstants(commandBuffer, &data, sizeof(Struct), offset);
}

/**
 * @brief Records a depth state override command.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param depthState Depth test/write configuration to bind.
 */
void cmdSetDepthState(const EOS::ICommandBuffer& commandBuffer, const EOS::DepthState& depthState);

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


/**
 * @brief Records a buffer update command.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param buffer The handle to the buffer we want to update.
 * @param size The size of data we want to update.
 * @param data The new data.
 * @param bufferOffset The offset inside the buffer from where you want to start the update.
 */
void cmdUpdateBuffer(const EOS::ICommandBuffer& commandBuffer, const EOS::BufferHandle& buffer, size_t size, const void* data, size_t bufferOffset);

/**
 * @brief Templated helper to record a typed buffer update command.
 * @tparam Struct Data type to copy into the destination buffer.
 * @param commandBuffer The commandbuffer we want to record into.
 * @param buffer The handle to the buffer we want to update.
 * @param data The new data.
 * @param bufferOffset The offset inside the buffer from where you want to start the update.
 */
template<typename Struct>
void cmdUpdateBuffer(const EOS::ICommandBuffer& commandBuffer, EOS::BufferHandle buffer, const Struct& data, size_t bufferOffset = 0)
{
    cmdUpdateBuffer(commandBuffer, buffer, sizeof(Struct), &data, bufferOffset);
}



#pragma endregion