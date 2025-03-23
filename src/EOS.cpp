#include "EOS.h"
#include "vulkan/vulkanClasses.h"

namespace EOS
{
    template <typename HandleType>
    Holder<HandleType>::Holder(IContext* ctx, HandleType hdl) noexcept
    : context{ctx}
    , handle{std::move(hdl)}
    {}

    template <typename HandleType>
    Holder<HandleType>::~Holder() noexcept
    {
        Reset();
    }

    template <typename HandleType>
    Holder<HandleType>::Holder(Holder&& other) noexcept
    : context{std::exchange(other.context, nullptr)}
    , handle{std::exchange(other.handle, HandleType{})}
    {}

    template <typename HandleType>
    Holder<HandleType>& Holder<HandleType>::operator=(Holder&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            context = std::exchange(other.context, nullptr);
            handle = std::exchange(other.handle, HandleType{});
        }

        return *this;
    }

    template <typename HandleType>
    void Holder<HandleType>::Reset() noexcept
    {
        if (context && handle.Valid())
        {
            //EOS::Destroy(context, handle);
        }

        context = nullptr;
        handle = HandleType{};
    }

    template <typename HandleType>
    Holder<HandleType>::operator HandleType() const noexcept
    {
        return handle;
    }

    template <typename HandleType>
    bool Holder<HandleType>::Empty() const noexcept
    {
        return handle.Empty();
    }

    template <typename HandleType>
    HandleType Holder<HandleType>::Release() noexcept
    {
        return std::exchange(handle, HandleType{});
    }

    template <typename HandleType>
    auto Holder<HandleType>::Gen() const noexcept
    {
        return handle.Gen();
    }

    template <typename HandleType>
    auto Holder<HandleType>::Index() const noexcept
    {
        return handle.Index();
    }

    template <typename HandleType>
    auto Holder<HandleType>::IndexAsVoid() const noexcept
    {
        return handle.IndexAsVoid();
    }

    template <typename HandleType>
    IContext* Holder<HandleType>::Context() const noexcept
    {
        return context;
    }

    template <typename HandleType>
    const HandleType& Holder<HandleType>::Get() const noexcept
    {
        return handle;
    }

    std::unique_ptr<IContext> CreateContextWithSwapchain(const ContextCreationDescription& contextCreationDescription)
    {
        return std::move( std::make_unique<VulkanContext>(contextCreationDescription) );
    }
}
