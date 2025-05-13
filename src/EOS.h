#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "defines.h"
#include "enums.h"
#include "handle.h"
#include "window.h"



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
        std::vector<uint32_t> spirv;
        EOS::ShaderStage shaderStage;
        uint32_t pushConstantSize;
        const char* debugName;
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
        * @brief Creates shader module from a compiled shader.
        * @param shaderInfo information about the shader such as its code and stage.
        * @return A Holder Handle to a shader module.
        */
        virtual EOS::Holder<EOS::ShaderModuleHandle> CreateShaderModule(const EOS::ShaderInfo& shaderInfo) = 0;

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
#pragma endregion