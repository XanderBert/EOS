#pragma once

#include <concepts>
#include <cstddef>
#include <utility>
#include <cstdint>

#include "defines.h"
#include "logger.h"


namespace EOS
{
    //Forward Declaring
    class IContext;

    template<typename ObjectType>
    class Handle final
    {
    public:
        Handle() = default;
        ~Handle() = default;
        Handle& operator=(const Handle&) = delete;

        Handle(const Handle& other)
        : Idx(other.Idx)
        , Generation(other.Gen())
        {}

        Handle(Handle&& other) noexcept
        : Idx(std::exchange(other.Idx, 0))
        , Generation(std::exchange(other.Generation, 0)) {}

        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other)
            {
                Idx = std::exchange(other.Idx, 0);
                Generation = std::exchange(other.Generation, 0);
            }
            return *this;
        }

        [[nodiscard]] inline bool Empty() const
        {
            return Generation == 0;
        }

        [[nodiscard]] inline bool Valid() const
        {
            return Generation != 0;
        }

        [[nodiscard]] inline uint32_t Index() const
        {
            return Idx;
        }

        [[nodiscard]] inline uint32_t Gen() const
        {
            return Generation;
        }

        [[nodiscard]] inline void* IndexAsVoid() const
        {
            return reinterpret_cast<void*>(static_cast<ptrdiff_t>(Idx));
        }

        [[nodiscard]] inline bool operator==(const Handle<ObjectType>& other) const
        {
            return Idx == other.Idx && Generation == other.Generation;
        }

        [[nodiscard]] inline bool operator!=(const Handle<ObjectType>& other) const
        {
            return !(*this == other);
        }

        explicit operator bool() const
        {
            return Generation != 0;
        }

    private:
        Handle(uint32_t index, uint32_t gen) : Idx(index), Generation(gen){};

        template<typename ObjectType_, typename ObjectType_Impl>
        friend class Pool;

        uint32_t Idx = 0;
        uint32_t Generation = 0;
    };
    static_assert(sizeof(Handle<class Foo>) == sizeof(uint64_t));

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

    template<typename HandleType>
    requires ValidHolder<HandleType>
    class Holder;

    //Create our Handle structures
    using ComputePipelineHandle     = Handle<struct ComputePipeline>;
    using RenderPipelineHandle      = Handle<struct RenderPipeline>;
    using RayTracingPipelineHandle  = Handle<struct RayTracingPipeline>;
    using ShaderProgramHandle       = Handle<struct ShaderProgram>;
    using SamplerHandle             = Handle<struct Sampler>;
    using BufferHandle              = Handle<struct Buffer>;
    using TextureHandle             = Handle<struct Texture>;
    using QueryPoolHandle           = Handle<struct QueryPool>;
    using AccelStructHandle         = Handle<struct AccelerationStructure>;

    using ComputePipelineHolder     = Holder<ComputePipelineHandle>;
    using RenderPipelineHolder      = Holder<RenderPipelineHandle>;
    using RayTracingPipelineHolder  = Holder<RayTracingPipelineHandle>;
    using ShaderProgramHolder       = Holder<ShaderProgramHandle>;
    using SamplerHolder             = Holder<SamplerHandle>;
    using BufferHolder              = Holder<BufferHandle>;
    using TextureHolder             = Holder<TextureHandle>;
    using QueryPoolHolder           = Holder<QueryPoolHandle>;
    using AccelStructHolder         = Holder<AccelStructHandle>;

    struct SubmitHandle final
    {
        SubmitHandle() = default;
        ~SubmitHandle() = default;
        explicit SubmitHandle(const uint64_t handle)
        : BufferIndex(static_cast<uint32_t>(handle & 0xffffffff))
        , ID(static_cast<uint32_t>(handle >> 32))
        {
            CHECK(ID, "The Handle ID is not valid");
        }

        [[nodiscard]] bool Empty() const
        {
            return ID == 0;
        }

        [[nodiscard]] uint64_t Handle() const
        {
            return (static_cast<uint64_t>(ID) << 32) + BufferIndex;
        }

        uint32_t BufferIndex = 0;
        uint32_t ID = 0;
    };
    static_assert(sizeof(SubmitHandle) == sizeof(uint64_t));
}
