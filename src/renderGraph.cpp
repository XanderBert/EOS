#include "renderGraph.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "logger.h"

namespace EOS
{
    namespace
    {
        enum class PassKind : uint8_t
        {
            Raster,
            Compute,
            Transfer,
        };

        enum class ResourceOrigin : uint8_t
        {
            Transient,      // created by the graph, contents do not survive the frame
            Imported,       // owned by the caller
            Swapchain,      // this frame's swapchain image, presented at the end
            History,        // owned by the graph, contents survive into the next frame
        };

        // How one PassBuilder call uses a resource.
        enum class AccessType : uint8_t
        {
            ColorTarget,
            DepthTarget,
            DepthTargetReadOnly,
            Sampled,
            Storage,
            CopySource,
            CopyDest,

            // Buffers from here on.
            BufferRead,
            BufferWrite,
            IndirectArguments,
            BufferCopySource,
            BufferCopyDest,
        };

        struct Access final
        {
            uint32_t Resource = 0;
            AccessType Type = AccessType::Sampled;
        };

        [[nodiscard]] constexpr bool IsBufferAccess(AccessType type)
        {
            return type >= AccessType::BufferRead;
        }

        [[nodiscard]] constexpr bool IsWrite(AccessType type)
        {
            switch (type)
            {
                case AccessType::ColorTarget:
                case AccessType::DepthTarget:
                case AccessType::Storage:
                case AccessType::CopyDest:
                case AccessType::BufferWrite:
                case AccessType::BufferCopyDest:
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] constexpr bool IsDepthFormat(Format format)
        {
            return format == Format::Z_UN16 || format == Format::Z_UN24 || format == Format::Z_F32 || format == Format::Z_UN24_S_UI8 || format == Format::Z_F32_S_UI8;
        }

        [[nodiscard]] constexpr uint32_t MarkerColor(PassKind kind)
        {
            switch (kind)
            {
                case PassKind::Raster:   return 0xff00f0ff;
                case PassKind::Compute:  return 0xffffaa00;
                case PassKind::Transfer: return 0xffaaaaaa;
            }

            return 0xffffffff;
        }

        // Pass functions are constructed in place in blocks that are reused every frame, so adding a pass allocates
        // nothing once the graph has seen its largest frame.
        class FunctionArena final
        {
        public:
            [[nodiscard]] void* Allocate(size_t size, size_t alignment)
            {
                if (size + alignment > kBlockSize)
                {
                    LargeAllocations.push_back(std::make_unique<std::byte[]>(size + alignment));
                    return AlignUp(LargeAllocations.back().get(), alignment);
                }

                while (true)
                {
                    if (CurrentBlock == Blocks.size()) Blocks.push_back(std::make_unique<std::byte[]>(kBlockSize));

                    std::byte* block = Blocks[CurrentBlock].get();
                    std::byte* memory = AlignUp(block + Used, alignment);
                    if (memory + size <= block + kBlockSize)
                    {
                        Used = static_cast<size_t>(memory + size - block);
                        return memory;
                    }

                    ++CurrentBlock;
                    Used = 0;
                }
            }

            void Reset()
            {
                CurrentBlock = 0;
                Used = 0;
                LargeAllocations.clear();
            }

        private:
            static constexpr size_t kBlockSize = 16 * 1024;

            [[nodiscard]] static std::byte* AlignUp(std::byte* pointer, size_t alignment)
            {
                const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
                return pointer + ((alignment - address % alignment) % alignment);
            }

            std::vector<std::unique_ptr<std::byte[]>> Blocks;
            size_t CurrentBlock = 0;
            size_t Used = 0;
            std::vector<std::unique_ptr<std::byte[]>> LargeAllocations;
        };

        // What makes two graph textures interchangeable in the pool.
        struct TextureKey final
        {
            ImageType Type = ImageType::Image_2D;
            Format TextureFormat = Format::Invalid;
            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t Depth = 0;
            uint32_t NumberOfLayers = 1;
            uint32_t NumberOfMipLevels = 1;
            uint32_t NumberOfSamples = 1;
            uint8_t Usage = 0;

            bool operator==(const TextureKey&) const = default;
        };

        [[nodiscard]] uint64_t ToKey(const TextureHandle& handle) { return static_cast<uint64_t>(handle.Gen()) << 32 | handle.Index(); }
        [[nodiscard]] uint64_t ToKey(const BufferHandle& handle) { return static_cast<uint64_t>(handle.Gen()) << 32 | handle.Index(); }

        // Pooled resources nothing used for this many frames are released. Enough to ride out a window being resized.
        constexpr uint64_t kFramesBeforeRelease = 8;
    }

    struct RenderGraph::Data final
    {
        struct TextureNode final
        {
            GraphTextureDescription Description;
            Dimensions Size;
            ResourceOrigin Origin = ResourceOrigin::Transient;
            uint32_t PersistentIndex = 0;           // into ImportedTextures or Histories
            uint8_t HistorySlot = 0;
            uint8_t Usage = 0;                      // TextureUsageFlags of the passes that survived culling
            TextureHandle Physical{};
            ResourceState State = ResourceState::Undefined;
            bool LastAccessWrote = false;
            bool Written = false;
            bool Needed = false;
        };

        struct BufferNode final
        {
            size_t Size = 0;
            const char* DebugName = "";
            ResourceOrigin Origin = ResourceOrigin::Transient;
            uint32_t PersistentIndex = 0;
            BufferHandle Physical{};
            ResourceState State = ResourceState::Undefined;
            bool LastAccessWrote = false;
            bool Needed = false;
        };

        struct PassNode final
        {
            const char* Name = "";
            PassKind Kind = PassKind::Raster;
            uint32_t FirstAccess = 0;
            uint32_t AccessCount = 0;
            uint32_t FirstColorTarget = 0;
            uint32_t ColorTargetCount = 0;
            uint32_t DepthTargetIndex = kNone;
            uint32_t FunctionIndex = kNone;
            bool NeverCull = false;
            bool Culled = false;
        };

        struct Function final
        {
            void* Object = nullptr;
            PassFunction Invoke = nullptr;
            DestroyFunction Destroy = nullptr;
        };

        struct PooledTexture final
        {
            TextureKey Key;
            Holder<TextureHandle> Texture;
            uint64_t LastUsedFrame = 0;
            bool InUse = false;
        };

        struct PooledBuffer final
        {
            size_t Size = 0;
            Holder<BufferHandle> Buffer;
            uint64_t LastUsedFrame = 0;
            bool InUse = false;
        };

        // The state the graph left an imported resource in, for the next frame that imports it.
        struct ImportedState final
        {
            uint64_t Handle = 0;
            ResourceState State = ResourceState::Undefined;
            uint64_t LastUsedFrame = 0;
        };

        struct History final
        {
            std::string Name;
            TextureKey Key;                         // without usage; Usage holds every usage seen so far
            uint8_t Usage = 0;
            Holder<TextureHandle> Textures[2];
            ResourceState States[2] = {ResourceState::Undefined, ResourceState::Undefined};
            uint8_t CurrentSlot = 0;
            uint64_t LastWrittenFrame = 0;
            uint64_t LastUsedFrame = 0;
            bool HasBeenWritten = false;
        };

        static constexpr uint32_t kNone = 0xFFFFFFFF;

        // This frame: flat arrays, cleared (not freed) every frame.
        std::vector<TextureNode> Textures;
        std::vector<BufferNode> Buffers;
        std::vector<PassNode> Passes;
        std::vector<Access> Accesses;
        std::vector<ColorTarget> ColorTargets;
        std::vector<DepthTarget> DepthTargets;
        std::vector<Function> Functions;
        FunctionArena Arena;
        GraphTexture Swapchain{};
        Dimensions SwapchainSize{};
        bool SwapchainSizeKnown = false;

        // Across frames.
        std::vector<PooledTexture> TexturePool;
        std::vector<PooledBuffer> BufferPool;
        std::vector<ImportedState> ImportedTextures;
        std::vector<ImportedState> ImportedBuffers;
        std::vector<History> Histories;
        uint64_t FrameNumber = 1;

        // Scratch, reused by every pass.
        std::vector<ImageBarrier> ImageBarriers;
        std::vector<GlobalBarrier> GlobalBarriers;

        // Each warning is logged once per pass, instead of every frame.
        std::set<std::pair<const void*, int>> Warned;

        void Warn(const char* passName, int warning, const std::string& message)
        {
            if (Warned.insert({passName, warning}).second) Logger->warn("Render graph, pass '{}': {}", passName, message);
        }

        [[nodiscard]] static uint32_t FindImported(std::vector<ImportedState>& states, uint64_t handle, uint64_t frame)
        {
            for (uint32_t i = 0; i < states.size(); ++i)
            {
                if (states[i].Handle == handle)
                {
                    states[i].LastUsedFrame = frame;
                    return i;
                }
            }

            states.push_back({.Handle = handle, .LastUsedFrame = frame});
            return static_cast<uint32_t>(states.size() - 1);
        }
    };

    // ---------------------------------------------------------------------------------------------------------------
    // PassContext

    DescriptorHandle PassContext::Descriptor(GraphTexture texture) const
    {
        return DescriptorHandle(Texture(texture));
    }

    TextureHandle PassContext::Texture(GraphTexture texture) const
    {
        CHECK(texture.Index < Graph.Graph->Textures.size(), "Invalid graph texture");
        return Graph.Graph->Textures[texture.Index].Physical;
    }

    Dimensions PassContext::Size(GraphTexture texture) const
    {
        return Graph.GetSize(texture);
    }

    BufferHandle PassContext::Buffer(GraphBuffer buffer) const
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid graph buffer");
        return Graph.Graph->Buffers[buffer.Index].Physical;
    }

    uint64_t PassContext::Address(GraphBuffer buffer, size_t offset) const
    {
        return Graph.Context->GetGPUAddress(Buffer(buffer), offset);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // PassBuilder: every call appends one access. Passes are declared one after the other, so a pass's accesses are
    // always the contiguous range at the end of the array.

    namespace
    {
        void AddAccess(RenderGraph::Data& graph, uint32_t passIndex, uint32_t resource, AccessType type)
        {
            CHECK(passIndex + 1 == graph.Passes.size(), "Declare a pass completely before adding the next one");
            graph.Accesses.push_back({.Resource = resource, .Type = type});
            ++graph.Passes[passIndex].AccessCount;
        }
    }

    PassBuilder& PassBuilder::Color(const ColorTarget& target)
    {
        RenderGraph::Data& graph = *Graph.Graph;
        RenderGraph::Data::PassNode& pass = graph.Passes[PassIndex];
        CHECK(pass.Kind == PassKind::Raster, "Only raster passes have color targets");
        CHECK(target.Texture.Index < graph.Textures.size(), "Invalid color target");
        CHECK(pass.ColorTargetCount < EOS_MAX_COLOR_ATTACHMENTS, "Too many color targets");

        if (pass.ColorTargetCount == 0) pass.FirstColorTarget = static_cast<uint32_t>(graph.ColorTargets.size());
        CHECK(pass.FirstColorTarget + pass.ColorTargetCount == graph.ColorTargets.size(), "Declare a pass completely before adding the next one");
        graph.ColorTargets.push_back(target);
        ++pass.ColorTargetCount;

        AddAccess(graph, PassIndex, target.Texture.Index, AccessType::ColorTarget);
        return *this;
    }

    PassBuilder& PassBuilder::Depth(const DepthTarget& target)
    {
        RenderGraph::Data& graph = *Graph.Graph;
        RenderGraph::Data::PassNode& pass = graph.Passes[PassIndex];
        CHECK(pass.Kind == PassKind::Raster, "Only raster passes have a depth target");
        CHECK(target.Texture.Index < graph.Textures.size(), "Invalid depth target");
        CHECK(pass.DepthTargetIndex == RenderGraph::Data::kNone, "A pass has one depth target");

        pass.DepthTargetIndex = static_cast<uint32_t>(graph.DepthTargets.size());
        graph.DepthTargets.push_back(target);

        AddAccess(graph, PassIndex, target.Texture.Index, target.ReadOnly ? AccessType::DepthTargetReadOnly : AccessType::DepthTarget);
        return *this;
    }

    PassBuilder& PassBuilder::Sample(GraphTexture texture)
    {
        CHECK(texture.Index < Graph.Graph->Textures.size(), "Invalid texture");
        AddAccess(*Graph.Graph, PassIndex, texture.Index, AccessType::Sampled);
        return *this;
    }

    PassBuilder& PassBuilder::Sample(std::initializer_list<GraphTexture> textures)
    {
        for (const GraphTexture texture : textures) Sample(texture);
        return *this;
    }

    PassBuilder& PassBuilder::Write(GraphTexture texture)
    {
        CHECK(texture.Index < Graph.Graph->Textures.size(), "Invalid texture");
        AddAccess(*Graph.Graph, PassIndex, texture.Index, AccessType::Storage);
        return *this;
    }

    PassBuilder& PassBuilder::CopyFrom(GraphTexture texture)
    {
        CHECK(texture.Index < Graph.Graph->Textures.size(), "Invalid texture");
        AddAccess(*Graph.Graph, PassIndex, texture.Index, AccessType::CopySource);
        return *this;
    }

    PassBuilder& PassBuilder::CopyTo(GraphTexture texture)
    {
        CHECK(texture.Index < Graph.Graph->Textures.size(), "Invalid texture");
        AddAccess(*Graph.Graph, PassIndex, texture.Index, AccessType::CopyDest);
        return *this;
    }

    PassBuilder& PassBuilder::Read(GraphBuffer buffer)
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid buffer");
        AddAccess(*Graph.Graph, PassIndex, buffer.Index, AccessType::BufferRead);
        return *this;
    }

    PassBuilder& PassBuilder::Read(std::initializer_list<GraphBuffer> buffers)
    {
        for (const GraphBuffer buffer : buffers) Read(buffer);
        return *this;
    }

    PassBuilder& PassBuilder::Write(GraphBuffer buffer)
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid buffer");
        AddAccess(*Graph.Graph, PassIndex, buffer.Index, AccessType::BufferWrite);
        return *this;
    }

    PassBuilder& PassBuilder::Indirect(GraphBuffer buffer)
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid buffer");
        AddAccess(*Graph.Graph, PassIndex, buffer.Index, AccessType::IndirectArguments);
        return *this;
    }

    PassBuilder& PassBuilder::CopyFrom(GraphBuffer buffer)
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid buffer");
        AddAccess(*Graph.Graph, PassIndex, buffer.Index, AccessType::BufferCopySource);
        return *this;
    }

    PassBuilder& PassBuilder::CopyTo(GraphBuffer buffer)
    {
        CHECK(buffer.Index < Graph.Graph->Buffers.size(), "Invalid buffer");
        AddAccess(*Graph.Graph, PassIndex, buffer.Index, AccessType::BufferCopyDest);
        return *this;
    }

    PassBuilder& PassBuilder::NeverCull()
    {
        Graph.Graph->Passes[PassIndex].NeverCull = true;
        return *this;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // RenderGraph: declaring

    RenderGraph::RenderGraph(IContext* context)
    : Context(context)
    , Graph(std::make_unique<Data>())
    {
    }

    RenderGraph::~RenderGraph()
    {
        for (const Data::Function& function : Graph->Functions)
        {
            if (function.Destroy) function.Destroy(function.Object);
        }
    }

    Dimensions RenderGraph::GetSwapchainSize() const
    {
        if (!Graph->SwapchainSizeKnown)
        {
            Graph->SwapchainSize = Context->GetSwapchainDimensions();
            Graph->SwapchainSizeKnown = true;
        }

        return Graph->SwapchainSize;
    }

    Dimensions RenderGraph::GetSize(GraphTexture texture) const
    {
        CHECK(texture.Index < Graph->Textures.size(), "Invalid graph texture");
        return Graph->Textures[texture.Index].Size;
    }

    GraphTexture RenderGraph::ImportSwapchain()
    {
        if (Graph->Swapchain.Valid()) return Graph->Swapchain;

        const Dimensions size = GetSwapchainSize();
        Graph->Textures.push_back(
        {
            .Description = {.TextureFormat = Context->GetSwapchainFormat(), .Size = size, .DebugName = "Swapchain"},
            .Size = size,
            .Origin = ResourceOrigin::Swapchain,
        });

        Graph->Swapchain = {static_cast<uint32_t>(Graph->Textures.size() - 1)};
        return Graph->Swapchain;
    }

    GraphTexture RenderGraph::ImportTexture(TextureHandle texture, const char* debugName)
    {
        CHECK(texture.Valid(), "Importing an invalid texture");
        const Dimensions size = Context->GetDimensions(texture);
        const uint32_t persistentIndex = Data::FindImported(Graph->ImportedTextures, ToKey(texture), Graph->FrameNumber);

        Graph->Textures.push_back(
        {
            .Description = {.TextureFormat = Context->GetFormat(texture), .Size = size, .DebugName = debugName},
            .Size = size,
            .Origin = ResourceOrigin::Imported,
            .PersistentIndex = persistentIndex,
            .Physical = texture,
        });

        return {static_cast<uint32_t>(Graph->Textures.size() - 1)};
    }

    GraphBuffer RenderGraph::ImportBuffer(BufferHandle buffer, const char* debugName)
    {
        CHECK(buffer.Valid(), "Importing an invalid buffer");
        const uint32_t persistentIndex = Data::FindImported(Graph->ImportedBuffers, ToKey(buffer), Graph->FrameNumber);

        Graph->Buffers.push_back(
        {
            .DebugName = debugName,
            .Origin = ResourceOrigin::Imported,
            .PersistentIndex = persistentIndex,
            .Physical = buffer,
        });

        return {static_cast<uint32_t>(Graph->Buffers.size() - 1)};
    }

    GraphTexture RenderGraph::CreateTexture(const GraphTextureDescription& description)
    {
        CHECK(description.TextureFormat != Format::Invalid, "A graph texture needs a format");

        Dimensions size = description.Size;
        if (size.Width == 0)
        {
            const Dimensions swapchainSize = GetSwapchainSize();
            size.Width = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(swapchainSize.Width) * description.Scale)));
            size.Height = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(swapchainSize.Height) * description.Scale)));
        }
        size.Height = std::max(1u, size.Height);
        size.Depth = std::max(1u, size.Depth);

        Graph->Textures.push_back({.Description = description, .Size = size});
        return {static_cast<uint32_t>(Graph->Textures.size() - 1)};
    }

    GraphBuffer RenderGraph::CreateBuffer(const GraphBufferDescription& description)
    {
        CHECK(description.Size > 0, "A graph buffer needs a size");
        Graph->Buffers.push_back({.Size = description.Size, .DebugName = description.DebugName});
        return {static_cast<uint32_t>(Graph->Buffers.size() - 1)};
    }

    HistoryTexture RenderGraph::CreateHistoryTexture(const char* name, const GraphTextureDescription& description)
    {
        const GraphTexture current = CreateTexture(description);
        const Data::TextureNode& node = Graph->Textures[current.Index];
        const TextureKey key
        {
            .Type = description.Type,
            .TextureFormat = description.TextureFormat,
            .Width = node.Size.Width,
            .Height = node.Size.Height,
            .Depth = node.Size.Depth,
            .NumberOfLayers = description.NumberOfLayers,
            .NumberOfMipLevels = description.NumberOfMipLevels,
            .NumberOfSamples = description.NumberOfSamples,
        };

        auto it = std::ranges::find_if(Graph->Histories, [name](const Data::History& history) { return history.Name == name; });
        if (it == Graph->Histories.end())
        {
            Graph->Histories.push_back({.Name = name, .Key = key});
            it = Graph->Histories.end() - 1;
        }
        else if (it->Key != key)
        {
            // Resized or changed: start over. The textures are recreated when the graph executes.
            it->Key = key;
            it->Textures[0] = nullptr;
            it->Textures[1] = nullptr;
            it->HasBeenWritten = false;
        }

        Data::History& history = *it;
        history.LastUsedFrame = Graph->FrameNumber;
        const uint32_t historyIndex = static_cast<uint32_t>(it - Graph->Histories.begin());

        Data::TextureNode& currentNode = Graph->Textures[current.Index];
        currentNode.Origin = ResourceOrigin::History;
        currentNode.PersistentIndex = historyIndex;
        currentNode.HistorySlot = history.CurrentSlot;

        Data::TextureNode previousNode = currentNode;
        previousNode.HistorySlot = static_cast<uint8_t>(1 - history.CurrentSlot);
        Graph->Textures.push_back(previousNode);
        const GraphTexture previous{static_cast<uint32_t>(Graph->Textures.size() - 1)};

        return {.Current = current, .Previous = previous, .PreviousIsValid = history.HasBeenWritten && history.LastWrittenFrame + 1 == Graph->FrameNumber};
    }

    PassBuilder RenderGraph::AddPass(const char* name, uint8_t kind)
    {
        Graph->Passes.push_back({.Name = name, .Kind = static_cast<PassKind>(kind), .FirstAccess = static_cast<uint32_t>(Graph->Accesses.size())});
        return PassBuilder(*this, static_cast<uint32_t>(Graph->Passes.size() - 1));
    }

    PassBuilder RenderGraph::AddRasterPass(const char* name) { return AddPass(name, static_cast<uint8_t>(PassKind::Raster)); }
    PassBuilder RenderGraph::AddComputePass(const char* name) { return AddPass(name, static_cast<uint8_t>(PassKind::Compute)); }
    PassBuilder RenderGraph::AddTransferPass(const char* name) { return AddPass(name, static_cast<uint8_t>(PassKind::Transfer)); }

    void* RenderGraph::AllocatePassFunction(size_t size, size_t alignment)
    {
        return Graph->Arena.Allocate(size, alignment);
    }

    void RenderGraph::SetPassFunction(uint32_t passIndex, void* function, PassFunction invoke, DestroyFunction destroy)
    {
        Data::PassNode& pass = Graph->Passes[passIndex];
        CHECK(pass.FunctionIndex == Data::kNone, "A pass has one function");

        pass.FunctionIndex = static_cast<uint32_t>(Graph->Functions.size());
        Graph->Functions.push_back({.Object = function, .Invoke = invoke, .Destroy = destroy});
    }

    // ---------------------------------------------------------------------------------------------------------------
    // RenderGraph: executing

    namespace
    {
        // The state a pass needs a resource in, from every way the pass uses it, and whether the pass writes it.
        struct RequiredState final
        {
            ResourceState State = ResourceState::Undefined;
            bool Writes = false;
        };

        [[nodiscard]] RequiredState GetTextureState(const RenderGraph::Data& graph, const RenderGraph::Data::PassNode& pass, uint32_t texture)
        {
            bool colorTarget = false;
            bool depthTarget = false;
            bool depthReadOnly = false;
            bool sampled = false;
            bool storage = false;
            bool copySource = false;
            bool copyDest = false;

            for (uint32_t i = pass.FirstAccess; i < pass.FirstAccess + pass.AccessCount; ++i)
            {
                const Access& access = graph.Accesses[i];
                if (IsBufferAccess(access.Type) || access.Resource != texture) continue;

                switch (access.Type)
                {
                    case AccessType::ColorTarget:         colorTarget = true; break;
                    case AccessType::DepthTarget:         depthTarget = true; break;
                    case AccessType::DepthTargetReadOnly: depthReadOnly = true; break;
                    case AccessType::Sampled:             sampled = true; break;
                    case AccessType::Storage:             storage = true; break;
                    case AccessType::CopySource:          copySource = true; break;
                    case AccessType::CopyDest:            copyDest = true; break;
                    default: break;
                }
            }

            const ResourceState storageState = pass.Kind == PassKind::Raster
                ? static_cast<ResourceState>(ResourceState::UnorderedAccess | ResourceState::UnorderedAccessPixel)
                : ResourceState::UnorderedAccess;

            if (colorTarget) return {ResourceState::RenderTarget, true};
            if (depthTarget) return {ResourceState::DepthWrite, true};
            if (depthReadOnly) return {ResourceState::DepthRead, false};   // the read-only depth layout can also be sampled
            if (storage) return {storageState, true};                      // GENERAL, which can also be sampled
            if (copyDest) return {ResourceState::CopyDest, true};
            if (copySource) return {ResourceState::CopySource, false};
            if (sampled) return {ResourceState::ShaderResource, false};
            return {};
        }

        [[nodiscard]] RequiredState GetBufferState(const RenderGraph::Data& graph, const RenderGraph::Data::PassNode& pass, uint32_t buffer)
        {
            uint32_t state = ResourceState::Undefined;
            bool writes = false;

            for (uint32_t i = pass.FirstAccess; i < pass.FirstAccess + pass.AccessCount; ++i)
            {
                const Access& access = graph.Accesses[i];
                if (!IsBufferAccess(access.Type) || access.Resource != buffer) continue;

                switch (access.Type)
                {
                    case AccessType::BufferRead:        state |= ResourceState::ShaderResource; break;
                    case AccessType::BufferWrite:       state |= pass.Kind == PassKind::Raster ? ResourceState::UnorderedAccess | ResourceState::UnorderedAccessPixel : ResourceState::UnorderedAccess; writes = true; break;
                    case AccessType::IndirectArguments: state |= ResourceState::IndirectArgument; break;
                    case AccessType::BufferCopySource:  state |= ResourceState::CopySource; break;
                    case AccessType::BufferCopyDest:    state |= ResourceState::CopyDest; writes = true; break;
                    default: break;
                }
            }

            return {static_cast<ResourceState>(state), writes};
        }

        [[nodiscard]] bool IsFirstAccessOfResourceInPass(const RenderGraph::Data& graph, const RenderGraph::Data::PassNode& pass, uint32_t accessIndex)
        {
            const Access& access = graph.Accesses[accessIndex];
            for (uint32_t i = pass.FirstAccess; i < accessIndex; ++i)
            {
                const Access& earlier = graph.Accesses[i];
                if (earlier.Resource == access.Resource && IsBufferAccess(earlier.Type) == IsBufferAccess(access.Type)) return false;
            }

            return true;
        }

        [[nodiscard]] uint8_t GetUsageFlag(AccessType type)
        {
            switch (type)
            {
                case AccessType::ColorTarget:
                case AccessType::DepthTarget:
                case AccessType::DepthTargetReadOnly: return TextureUsageFlags::Attachment;
                case AccessType::Sampled:             return TextureUsageFlags::Sampled;
                case AccessType::Storage:             return TextureUsageFlags::Storage;
                default:                              return 0;
            }
        }
    }

    SubmitHandle RenderGraph::Execute()
    {
        EOS_PROFILER_FUNCTION();
        Data& graph = *Graph;
        const uint64_t frame = graph.FrameNumber;

        // Cull: walking backwards, a pass is kept when it writes something outside the graph or something a kept pass
        // uses later. Passes without any access may have effects the graph cannot see, so they are kept too.
        for (auto pass = graph.Passes.rbegin(); pass != graph.Passes.rend(); ++pass)
        {
            bool needed = pass->NeverCull || pass->AccessCount == 0;
            for (uint32_t i = pass->FirstAccess; i < pass->FirstAccess + pass->AccessCount && !needed; ++i)
            {
                const Access& access = graph.Accesses[i];
                if (!IsWrite(access.Type)) continue;

                const bool needs = IsBufferAccess(access.Type)
                    ? graph.Buffers[access.Resource].Origin != ResourceOrigin::Transient || graph.Buffers[access.Resource].Needed
                    : graph.Textures[access.Resource].Origin != ResourceOrigin::Transient || graph.Textures[access.Resource].Needed;
                needed = needed || needs;
            }

            pass->Culled = !needed;
            if (!needed) continue;

            for (uint32_t i = pass->FirstAccess; i < pass->FirstAccess + pass->AccessCount; ++i)
            {
                const Access& access = graph.Accesses[i];
                if (IsBufferAccess(access.Type)) graph.Buffers[access.Resource].Needed = true;
                else graph.Textures[access.Resource].Needed = true;
            }
        }

        // Usage flags follow from how the kept passes use each texture.
        for (const Data::PassNode& pass : graph.Passes)
        {
            if (pass.Culled) continue;
            for (uint32_t i = pass.FirstAccess; i < pass.FirstAccess + pass.AccessCount; ++i)
            {
                const Access& access = graph.Accesses[i];
                if (!IsBufferAccess(access.Type)) graph.Textures[access.Resource].Usage |= GetUsageFlag(access.Type);
            }
        }

        // A history texture keeps every usage it has seen. Growing it means recreating its textures, which loses their
        // contents, so it is settled before anything gets a physical texture.
        for (const Data::TextureNode& texture : graph.Textures)
        {
            if (!texture.Needed || texture.Origin != ResourceOrigin::History) continue;

            Data::History& history = graph.Histories[texture.PersistentIndex];
            if ((history.Usage | texture.Usage) == history.Usage) continue;

            history.Usage |= texture.Usage;
            history.Textures[0] = nullptr;
            history.Textures[1] = nullptr;
            history.HasBeenWritten = false;
        }

        // Physical resources.
        for (Data::TextureNode& texture : graph.Textures)
        {
            if (!texture.Needed) continue;

            const TextureKey key
            {
                .Type = texture.Description.Type,
                .TextureFormat = texture.Description.TextureFormat,
                .Width = texture.Size.Width,
                .Height = texture.Size.Height,
                .Depth = texture.Size.Depth,
                .NumberOfLayers = texture.Description.NumberOfLayers,
                .NumberOfMipLevels = texture.Description.NumberOfMipLevels,
                .NumberOfSamples = texture.Description.NumberOfSamples,
                .Usage = texture.Usage,
            };

            const auto createTexture = [&](const char* debugName)
            {
                return Context->CreateTexture(
                {
                    .Type = key.Type,
                    .TextureFormat = key.TextureFormat,
                    .TextureDimensions = {key.Width, key.Height, key.Depth},
                    .NumberOfLayers = key.NumberOfLayers,
                    .NumberOfMipLevels = key.NumberOfMipLevels,
                    .NumberOfSamples = key.NumberOfSamples,
                    .Usage = key.Usage,
                    .DebugName = debugName,
                });
            };

            switch (texture.Origin)
            {
                case ResourceOrigin::Transient:
                {
                    auto pooled = std::ranges::find_if(graph.TexturePool, [&key](const Data::PooledTexture& entry) { return !entry.InUse && entry.Key == key; });
                    if (pooled == graph.TexturePool.end())
                    {
                        graph.TexturePool.push_back({.Key = key, .Texture = createTexture(texture.Description.DebugName)});
                        pooled = graph.TexturePool.end() - 1;
                    }

                    pooled->InUse = true;
                    pooled->LastUsedFrame = frame;
                    texture.Physical = static_cast<TextureHandle>(pooled->Texture);
                    texture.State = ResourceState::Undefined;
                    break;
                }

                case ResourceOrigin::History:
                {
                    Data::History& history = graph.Histories[texture.PersistentIndex];
                    for (uint8_t slot = 0; slot < 2; ++slot)
                    {
                        if (history.Textures[slot].Valid()) continue;

                        TextureKey historyKey = key;
                        historyKey.Usage = history.Usage;
                        history.Textures[slot] = Context->CreateTexture(
                        {
                            .Type = historyKey.Type,
                            .TextureFormat = historyKey.TextureFormat,
                            .TextureDimensions = {historyKey.Width, historyKey.Height, historyKey.Depth},
                            .NumberOfLayers = historyKey.NumberOfLayers,
                            .NumberOfMipLevels = historyKey.NumberOfMipLevels,
                            .NumberOfSamples = historyKey.NumberOfSamples,
                            .Usage = historyKey.Usage,
                            .DebugName = history.Name.c_str(),
                        });
                        history.States[slot] = ResourceState::Undefined;
                    }

                    texture.Physical = static_cast<TextureHandle>(history.Textures[texture.HistorySlot]);
                    texture.State = history.States[texture.HistorySlot];
                    break;
                }

                case ResourceOrigin::Imported:
                    texture.State = graph.ImportedTextures[texture.PersistentIndex].State;
                    break;

                case ResourceOrigin::Swapchain:
                    texture.State = ResourceState::Undefined;   // acquired below, once the command buffer exists
                    break;
            }
        }

        for (Data::BufferNode& buffer : graph.Buffers)
        {
            if (!buffer.Needed) continue;

            if (buffer.Origin == ResourceOrigin::Transient)
            {
                auto pooled = std::ranges::find_if(graph.BufferPool, [&buffer](const Data::PooledBuffer& entry) { return !entry.InUse && entry.Size == buffer.Size; });
                if (pooled == graph.BufferPool.end())
                {
                    graph.BufferPool.push_back(
                    {
                        .Size = buffer.Size,
                        .Buffer = Context->CreateBuffer({.Usage = static_cast<BufferUsageFlags>(BufferUsageFlags::StorageFlag | BufferUsageFlags::Indirect), .Storage = StorageType::Device, .Size = buffer.Size, .DebugName = buffer.DebugName}),
                    });
                    pooled = graph.BufferPool.end() - 1;
                }

                pooled->InUse = true;
                pooled->LastUsedFrame = frame;
                buffer.Physical = static_cast<BufferHandle>(pooled->Buffer);
                buffer.State = ResourceState::Undefined;
            }
            else
            {
                buffer.State = graph.ImportedBuffers[buffer.PersistentIndex].State;
            }
        }

        ICommandBuffer& commandBuffer = Context->AcquireCommandBuffer();
        if (graph.Swapchain.Valid())
        {
            Data::TextureNode& swapchain = graph.Textures[graph.Swapchain.Index];
            swapchain.Physical = Context->GetSwapChainTexture();
            swapchain.Needed = true;
        }

        PassContext context(commandBuffer, *this);
        for (const Data::PassNode& pass : graph.Passes)
        {
            if (pass.Culled) continue;

            // Barriers: every resource of the pass moves into the state the pass needs, in one batch. Writes also need a
            // barrier when the state stays the same (storage after storage, a target rendered to twice).
            graph.ImageBarriers.clear();
            graph.GlobalBarriers.clear();
            for (uint32_t i = pass.FirstAccess; i < pass.FirstAccess + pass.AccessCount; ++i)
            {
                if (!IsFirstAccessOfResourceInPass(graph, pass, i)) continue;

                const Access& access = graph.Accesses[i];
                if (IsBufferAccess(access.Type))
                {
                    Data::BufferNode& buffer = graph.Buffers[access.Resource];
                    const RequiredState required = GetBufferState(graph, pass, access.Resource);
                    const bool needsBarrier = buffer.State != ResourceState::Undefined && (buffer.State != required.State || buffer.LastAccessWrote || required.Writes);
                    if (needsBarrier) graph.GlobalBarriers.push_back({.Buffer = buffer.Physical, .CurrentState = buffer.State, .NextState = required.State});

                    buffer.State = required.State;
                    buffer.LastAccessWrote = required.Writes;
                    continue;
                }

                Data::TextureNode& texture = graph.Textures[access.Resource];
                const RequiredState required = GetTextureState(graph, pass, access.Resource);
                if (!required.Writes && !texture.Written && texture.Origin == ResourceOrigin::Transient)
                {
                    graph.Warn(pass.Name, 0, std::string("reads '") + texture.Description.DebugName + "' before any pass wrote it this frame");
                }

                const bool needsBarrier = texture.State != required.State || texture.LastAccessWrote || required.Writes;
                if (needsBarrier) graph.ImageBarriers.push_back({.Texture = texture.Physical, .CurrentState = texture.State, .NextState = required.State});

                texture.State = required.State;
                texture.LastAccessWrote = required.Writes;
                texture.Written = texture.Written || required.Writes;
            }

            if (!graph.ImageBarriers.empty() || !graph.GlobalBarriers.empty()) cmdPipelineBarrier(commandBuffer, graph.GlobalBarriers, graph.ImageBarriers);

            cmdPushMarker(commandBuffer, pass.Name, MarkerColor(pass.Kind));

            if (pass.Kind == PassKind::Raster)
            {
                RenderPass renderPass{};
                Framebuffer framebuffer{.DebugName = pass.Name};

                for (uint32_t i = 0; i < pass.ColorTargetCount; ++i)
                {
                    const ColorTarget& target = graph.ColorTargets[pass.FirstColorTarget + i];
                    LoadOp load = target.Load;

                    // A graph texture holds nothing at the start of the frame, so loading it is never meaningful.
                    if (load == LoadOp::Load && graph.Textures[target.Texture.Index].Origin == ResourceOrigin::Transient && !graph.Textures[target.Texture.Index].Written)
                    {
                        load = LoadOp::DontCare;
                    }

                    renderPass.Color[i] =
                    {
                        .LoadOpState = load,
                        .StoreOpState = target.Store,
                        .Layer = target.Layer,
                        .LayerCount = target.LayerCount,
                        .Level = target.Level,
                        .ClearColor = {target.ClearColor[0], target.ClearColor[1], target.ClearColor[2], target.ClearColor[3]},
                    };
                    framebuffer.Color[i].Texture = TextureHandle(graph.Textures[target.Texture.Index].Physical);
                }

                if (pass.DepthTargetIndex != Data::kNone)
                {
                    const DepthTarget& target = graph.DepthTargets[pass.DepthTargetIndex];
                    renderPass.Depth =
                    {
                        .LoadOpState = target.Load,
                        .StoreOpState = target.Store,
                        .Layer = target.Layer,
                        .LayerCount = target.LayerCount,
                        .ClearDepth = target.ClearDepth,
                        .ReadOnly = target.ReadOnly,
                    };
                    framebuffer.DepthStencil.Texture = TextureHandle(graph.Textures[target.Texture.Index].Physical);

                    if (!IsDepthFormat(graph.Textures[target.Texture.Index].Description.TextureFormat))
                    {
                        graph.Warn(pass.Name, 1, std::string("uses '") + graph.Textures[target.Texture.Index].Description.DebugName + "' as depth target, but it has no depth format");
                    }
                }

                CHECK(pass.ColorTargetCount > 0 || pass.DepthTargetIndex != Data::kNone, "A raster pass needs a color or depth target");
                cmdBeginRendering(commandBuffer, renderPass, framebuffer);
                if (pass.FunctionIndex != Data::kNone) graph.Functions[pass.FunctionIndex].Invoke(graph.Functions[pass.FunctionIndex].Object, context);
                cmdEndRendering(commandBuffer);
            }
            else if (pass.FunctionIndex != Data::kNone)
            {
                graph.Functions[pass.FunctionIndex].Invoke(graph.Functions[pass.FunctionIndex].Object, context);
            }

            cmdPopMarker(commandBuffer);
        }

        // The swapchain image is handed to presentation; everything else keeps the state it ended in for the next frame.
        TextureHandle presentTexture{};
        if (graph.Swapchain.Valid())
        {
            Data::TextureNode& swapchain = graph.Textures[graph.Swapchain.Index];
            cmdPipelineBarrier(commandBuffer, {}, {{.Texture = swapchain.Physical, .CurrentState = swapchain.State, .NextState = ResourceState::Present}});
            presentTexture = TextureHandle(swapchain.Physical);
        }

        for (const Data::TextureNode& texture : graph.Textures)
        {
            if (!texture.Needed) continue;

            if (texture.Origin == ResourceOrigin::Imported)
            {
                graph.ImportedTextures[texture.PersistentIndex].State = texture.State;
            }
            else if (texture.Origin == ResourceOrigin::History)
            {
                Data::History& history = graph.Histories[texture.PersistentIndex];
                history.States[texture.HistorySlot] = texture.State;
                if (texture.Written && texture.HistorySlot == history.CurrentSlot)
                {
                    history.HasBeenWritten = true;
                    history.LastWrittenFrame = frame;
                }
            }
        }

        for (const Data::BufferNode& buffer : graph.Buffers)
        {
            if (buffer.Needed && buffer.Origin == ResourceOrigin::Imported) graph.ImportedBuffers[buffer.PersistentIndex].State = buffer.State;
        }

        const SubmitHandle submitHandle = Context->Submit(commandBuffer, presentTexture);

        // Ready for the next frame: history textures swap, unused pooled resources are released, the frame data is
        // cleared but keeps its memory.
        for (Data::History& history : graph.Histories)
        {
            if (history.LastUsedFrame == frame) history.CurrentSlot = static_cast<uint8_t>(1 - history.CurrentSlot);
        }

        std::erase_if(graph.Histories, [frame](const Data::History& history) { return history.LastUsedFrame + kFramesBeforeRelease < frame; });
        std::erase_if(graph.TexturePool, [frame](const Data::PooledTexture& entry) { return entry.LastUsedFrame + kFramesBeforeRelease < frame; });
        std::erase_if(graph.BufferPool, [frame](const Data::PooledBuffer& entry) { return entry.LastUsedFrame + kFramesBeforeRelease < frame; });
        std::erase_if(graph.ImportedTextures, [frame](const Data::ImportedState& entry) { return entry.LastUsedFrame + kFramesBeforeRelease < frame; });
        std::erase_if(graph.ImportedBuffers, [frame](const Data::ImportedState& entry) { return entry.LastUsedFrame + kFramesBeforeRelease < frame; });
        for (Data::PooledTexture& entry : graph.TexturePool) entry.InUse = false;
        for (Data::PooledBuffer& entry : graph.BufferPool) entry.InUse = false;

        for (const Data::Function& function : graph.Functions)
        {
            if (function.Destroy) function.Destroy(function.Object);
        }

        graph.Textures.clear();
        graph.Buffers.clear();
        graph.Passes.clear();
        graph.Accesses.clear();
        graph.ColorTargets.clear();
        graph.DepthTargets.clear();
        graph.Functions.clear();
        graph.Arena.Reset();
        graph.Swapchain = {};
        graph.SwapchainSizeKnown = false;
        ++graph.FrameNumber;

        return submitHandle;
    }
}
