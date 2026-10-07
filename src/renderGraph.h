#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "EOS.h"

// A render graph rebuilt every frame (as Frostbite's FrameGraph and Unreal's RDG are): passes declare what they read
// and write, and Execute() derives the rest. It orders nothing itself (passes run in the order they were added, which
// is already an order where every resource is written before it is read), but it removes passes whose results nothing
// uses, creates and pools the textures and buffers the graph owns, recreates them when the window is resized, inserts
// every barrier, begins and ends rendering for raster passes, and names each pass in debug markers.
//
//     EOS::RenderGraph& graph = ...;
//     const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
//     const EOS::GraphTexture albedo = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "Albedo"});
//     const EOS::GraphTexture depth = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
//
//     graph.AddRasterPass("GBuffer").Color(EOS::Clear(albedo)).Depth(EOS::ClearDepth(depth)).Execute([&](EOS::PassContext& pass)
//     {
//         cmdBindRenderPipeline(pass.Cmd, pipeline);
//         cmdDraw(pass.Cmd, 3);
//     });
//     graph.AddRasterPass("Present").Color(EOS::Clear(backbuffer)).Sample(albedo).Execute([&](EOS::PassContext& pass)
//     {
//         cmdPushConstants(pass.Cmd, PresentPC{.sceneColor = pass.Descriptor(albedo)});
//         cmdDraw(pass.Cmd, 3);
//     });
//     graph.Execute();
//
// Internally the graph is flat data: arrays of resources, passes and accesses indexed by the handles below, and the
// pass functions live in an arena that is reused every frame, so building a graph allocates nothing once it is warm.
namespace EOS
{
    class RenderGraph;

    /**
     * @brief A texture of the graph being built. Only valid until the graph's Execute().
     */
    struct GraphTexture final
    {
        static constexpr uint32_t InvalidIndex = 0xFFFFFFFF;
        uint32_t Index = InvalidIndex;

        [[nodiscard]] bool Valid() const { return Index != InvalidIndex; }
        bool operator==(const GraphTexture&) const = default;
    };

    /**
     * @brief A buffer of the graph being built. Only valid until the graph's Execute().
     */
    struct GraphBuffer final
    {
        static constexpr uint32_t InvalidIndex = 0xFFFFFFFF;
        uint32_t Index = InvalidIndex;

        [[nodiscard]] bool Valid() const { return Index != InvalidIndex; }
        bool operator==(const GraphBuffer&) const = default;
    };

    /**
     * @brief A texture the graph creates. Its usage flags follow from how passes use it.
     */
    struct GraphTextureDescription final
    {
        Format TextureFormat = Format::Invalid;
        float Scale = 1.0f;                     // of the swapchain size, rounded up; ignored when Size is set
        Dimensions Size{0, 0, 0};               // a fixed size, for textures that do not follow the window
        ImageType Type = ImageType::Image_2D;
        uint32_t NumberOfLayers = 1;
        uint32_t NumberOfMipLevels = 1;
        uint32_t NumberOfSamples = 1;
        const char* DebugName = "";             // has to stay valid until Execute()
    };

    /**
     * @brief A buffer the graph creates, for data passes hand to each other within a frame.
     */
    struct GraphBufferDescription final
    {
        size_t Size = 0;
        const char* DebugName = "";             // has to stay valid until Execute()
    };

    /**
     * @brief The two textures of a history texture: this frame's, and the one written last frame.
     */
    struct HistoryTexture final
    {
        GraphTexture Current;
        GraphTexture Previous;
        bool PreviousIsValid = false;           // false on the first frame and after the texture was recreated (resize)
    };

    /**
     * @brief A color texture a raster pass renders into.
     */
    struct ColorTarget final
    {
        GraphTexture Texture;
        LoadOp Load = LoadOp::Load;
        StoreOp Store = StoreOp::Store;
        std::array<float, 4> ClearColor{0.0f, 0.0f, 0.0f, 0.0f};
        uint8_t Layer = 0;
        uint8_t LayerCount = 1;
        uint8_t Level = 0;
    };

    /**
     * @brief The depth texture a raster pass tests against, and writes unless ReadOnly.
     */
    struct DepthTarget final
    {
        GraphTexture Texture;
        LoadOp Load = LoadOp::Load;
        StoreOp Store = StoreOp::Store;
        float ClearDepth = 1.0f;
        uint8_t Layer = 0;
        uint8_t LayerCount = 1;
        bool ReadOnly = false;                  // the pass only tests depth, so it can also sample the texture
    };

    [[nodiscard]] constexpr ColorTarget Clear(GraphTexture texture, std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 0.0f})
    {
        return {.Texture = texture, .Load = LoadOp::Clear, .ClearColor = color};
    }

    [[nodiscard]] constexpr ColorTarget Load(GraphTexture texture)
    {
        return {.Texture = texture, .Load = LoadOp::Load};
    }

    [[nodiscard]] constexpr DepthTarget ClearDepth(GraphTexture texture, float depth = 1.0f)
    {
        return {.Texture = texture, .Load = LoadOp::Clear, .ClearDepth = depth};
    }

    [[nodiscard]] constexpr DepthTarget LoadDepth(GraphTexture texture, bool readOnly = false)
    {
        return {.Texture = texture, .Load = LoadOp::Load, .ReadOnly = readOnly};
    }

    /**
     * @brief What a pass function gets: the command buffer and the real resources behind the graph's handles.
     */
    class PassContext final
    {
    public:
        ICommandBuffer& Cmd;

        // The bindless handle shaders read or write the texture through.
        [[nodiscard]] DescriptorHandle Descriptor(GraphTexture texture) const;
        [[nodiscard]] TextureHandle Texture(GraphTexture texture) const;
        [[nodiscard]] Dimensions Size(GraphTexture texture) const;

        [[nodiscard]] BufferHandle Buffer(GraphBuffer buffer) const;
        [[nodiscard]] uint64_t Address(GraphBuffer buffer, size_t offset = 0) const;

    private:
        friend class RenderGraph;
        PassContext(ICommandBuffer& commandBuffer, const RenderGraph& graph) : Cmd(commandBuffer), Graph(graph) {}

        const RenderGraph& Graph;
    };

    /**
     * @brief Declares what a pass uses. Returned by RenderGraph::Add*Pass; every call records one access.
     */
    class PassBuilder final
    {
    public:
        PassBuilder& Color(const ColorTarget& target);          // raster passes
        PassBuilder& Depth(const DepthTarget& target);          // raster passes

        PassBuilder& Sample(GraphTexture texture);              // read by shaders
        PassBuilder& Sample(std::initializer_list<GraphTexture> textures);
        PassBuilder& Write(GraphTexture texture);               // storage image, read and written by shaders
        PassBuilder& CopyFrom(GraphTexture texture);
        PassBuilder& CopyTo(GraphTexture texture);

        PassBuilder& Read(GraphBuffer buffer);                  // read by shaders through its address
        PassBuilder& Read(std::initializer_list<GraphBuffer> buffers);
        PassBuilder& Write(GraphBuffer buffer);                 // read and written by shaders through its address
        PassBuilder& Indirect(GraphBuffer buffer);              // draw or dispatch arguments
        PassBuilder& CopyFrom(GraphBuffer buffer);
        PassBuilder& CopyTo(GraphBuffer buffer);                // includes cmdUpdateBuffer

        // Keeps the pass even when nothing uses what it writes (it has effects the graph cannot see).
        PassBuilder& NeverCull();

        /**
         * @brief Sets what the pass records. Called during Execute(), after the barriers it needs and, for a raster
         *        pass, inside cmdBeginRendering for its targets. A pass without a function still clears its targets.
         */
        template<typename Function>
        void Execute(Function&& function);

    private:
        friend class RenderGraph;
        PassBuilder(RenderGraph& graph, uint32_t passIndex) : Graph(graph), PassIndex(passIndex) {}

        RenderGraph& Graph;
        uint32_t PassIndex;
    };

    /**
     * @brief See the top of this file.
     */
    class RenderGraph final
    {
    public:
        explicit RenderGraph(IContext* context);
        ~RenderGraph();
        DELETE_COPY_MOVE(RenderGraph)

        // This frame's swapchain image. Execute() presents it.
        [[nodiscard]] GraphTexture ImportSwapchain();

        // Resources that live outside the graph. The graph remembers the state it left them in for the next frame.
        [[nodiscard]] GraphTexture ImportTexture(TextureHandle texture, const char* debugName = "");
        [[nodiscard]] GraphBuffer ImportBuffer(BufferHandle buffer, const char* debugName = "");

        // Owned by the graph and pooled across frames. Their contents do not survive the frame.
        [[nodiscard]] GraphTexture CreateTexture(const GraphTextureDescription& description);
        [[nodiscard]] GraphBuffer CreateBuffer(const GraphBufferDescription& description);

        // A texture that keeps its contents into the next frame, for temporal effects. Identified by its name.
        [[nodiscard]] HistoryTexture CreateHistoryTexture(const char* name, const GraphTextureDescription& description);

        [[nodiscard]] PassBuilder AddRasterPass(const char* name);
        [[nodiscard]] PassBuilder AddComputePass(const char* name);
        [[nodiscard]] PassBuilder AddTransferPass(const char* name);

        /**
         * @brief Writes data into buffer on the GPU, in order with the passes around it (a transfer pass running
         *        cmdUpdateBuffer, so at most 64 KiB). The data is copied, so it need not outlive the call.
         * @note  Per-frame data has to be written this way rather than from the CPU: a CPU write into memory the GPU is
         *        still reading for an earlier frame changes it under that frame's passes.
         */
        template<typename Payload>
        void AddUpload(const char* name, GraphBuffer buffer, const Payload& data, size_t offset = 0);

        // The size a texture of this frame has (known as soon as it is declared).
        [[nodiscard]] Dimensions GetSize(GraphTexture texture) const;
        [[nodiscard]] Dimensions GetSwapchainSize() const;

        /**
         * @brief Removes unused passes, creates the resources, records every pass with its barriers and submits,
         *        presenting the swapchain image when one was imported. Afterwards the graph is empty for the next frame.
         */
        SubmitHandle Execute();

        // The graph's storage. Only defined in renderGraph.cpp.
        struct Data;

    private:
        friend class PassBuilder;
        friend class PassContext;

        using PassFunction = void (*)(void* function, PassContext& context);
        using DestroyFunction = void (*)(void* function);

        [[nodiscard]] PassBuilder AddPass(const char* name, uint8_t kind);
        [[nodiscard]] void* AllocatePassFunction(size_t size, size_t alignment);
        void SetPassFunction(uint32_t passIndex, void* function, PassFunction invoke, DestroyFunction destroy);

        IContext* Context;
        std::unique_ptr<Data> Graph;
    };

    template<typename Payload>
    void RenderGraph::AddUpload(const char* name, GraphBuffer buffer, const Payload& data, size_t offset)
    {
        static_assert(std::is_trivially_copyable_v<Payload>, "Uploaded data is copied byte for byte");
        static_assert(sizeof(Payload) <= 65536 && sizeof(Payload) % 4 == 0, "cmdUpdateBuffer takes at most 64 KiB, in multiples of 4 bytes");

        AddTransferPass(name).CopyTo(buffer).Execute([buffer, data, offset](PassContext& pass)
        {
            cmdUpdateBuffer(pass.Cmd, pass.Buffer(buffer), data, offset);
        });
    }

    template<typename Function>
    void PassBuilder::Execute(Function&& function)
    {
        using Callable = std::decay_t<Function>;
        static_assert(std::is_invocable_v<Callable&, PassContext&>, "A pass function takes an EOS::PassContext&");

        void* memory = Graph.AllocatePassFunction(sizeof(Callable), alignof(Callable));
        Callable* callable = new (memory) Callable(std::forward<Function>(function));

        RenderGraph::DestroyFunction destroy = nullptr;
        if constexpr (!std::is_trivially_destructible_v<Callable>)
        {
            destroy = [](void* object) { static_cast<Callable*>(object)->~Callable(); };
        }

        Graph.SetPassFunction(PassIndex, callable, [](void* object, PassContext& context) { (*static_cast<Callable*>(object))(context); }, destroy);
    }
}
