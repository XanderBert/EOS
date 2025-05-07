#pragma once

#include <memory>
#include <string>
#include <vector>

#include "defines.h"
#include "enums.h"
#include "handle.h"
#include "window.h"

#include "shaders/shaderUtils.h"

namespace EOS
{
    //TODO: Almost everything here will need Doxygen documentation, as the end user will work with these functions
    //I would also like to return my own type of VkResult on functions for the end user.

    //Forward declare
    class IContext;

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

    struct HardwareDeviceDescription
    {
        uintptr_t id{};
        HardwareDeviceType type { HardwareDeviceType::Integrated} ;
        std::string name{};
    };

    struct ContextConfiguration final
    {
        bool enableValidationLayers{ true };
        ColorSpace DesiredSwapChainColorSpace { ColorSpace::SRGB_Linear };
    };

    struct ContextCreationDescription final
    {
        ContextConfiguration    config;
        HardwareDeviceType      preferredHardwareType{HardwareDeviceType::Discrete};
        const char*             applicationName{};
        void*                   window{};
        void*                   display{};
    };

    //used for buffer StateTransitioning without changing the index queue
    struct GlobalBarrier
    {
        const BufferHandle      Buffer;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
    };

    struct ImageBarrier
    {
        const TextureHandle    Texture;
        const ResourceState     CurrentState;
        const ResourceState     NextState;
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
        * @brief
        * @return
        */
        virtual ICommandBuffer& AcquireCommandBuffer() = 0;

        /**
        * @brief
        * @return
        */
        virtual SubmitHandle Submit(ICommandBuffer& commandBuffer, TextureHandle present) = 0;

        /**
         * @brief 
         * @return 
         */
        virtual TextureHandle GetSwapChainTexture() = 0;

        /**
        * @brief
        * @return
        */
        virtual void Destroy(TextureHandle handle) = 0;


        /**
        * @brief
        * @return
        */
        virtual void Destroy(ShaderModuleHandle handle) = 0;

    protected:
        IContext() = default;
    };
#pragma endregion



    // Concept for required HandleType operations
    template<typename T>
    concept ValidHolder = requires(T t)
    {
        { t.Valid() } -> std::convertible_to<bool>;
        { t.Empty() } -> std::convertible_to<bool>;
        { t.Gen() } -> std::convertible_to<uint32_t>;
        { t.Index() } -> std::convertible_to<uint32_t>;
        { t.IndexAsVoid() } -> std::convertible_to<void*>;
    };

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


    std::unique_ptr<IContext> CreateContextWithSwapChain(const ContextCreationDescription& contextCreationDescription);
    std::unique_ptr<ShaderCompiler> CreateShaderCompiler(const std::filesystem::path& shaderFolder);
}






#pragma region GLOBAL_FUNCTIONS

/**
* @brief
* @return
*/
void cmdPipelineBarrier(const EOS::ICommandBuffer& commandBuffer, const std::vector<EOS::GlobalBarrier>& globalBarriers, const std::vector<EOS::ImageBarrier>& imageBarriers);

/**
* @brief
* @return
*/
EOS::Holder<EOS::ShaderModuleHandle> LoadShader(const std::unique_ptr<EOS::IContext>& context, const char* fileName);

#pragma endregion