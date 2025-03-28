#include "Handle.h"

namespace EOS
{
    template <typename HandleType>
    Handle<HandleType>::Handle(IContext* ctx, HandleType hdl) noexcept
    : context{ctx}
    , handle{std::move(hdl)}
    {}

    template <typename HandleType>
    Handle<HandleType>::~Handle() noexcept
    {
        Reset();
    }

    template <typename HandleType>
    Handle<HandleType>::Handle(Handle&& other) noexcept
    : context{std::exchange(other.context, nullptr)}
    , handle{std::exchange(other.handle, HandleType{})}
    {}

    template <typename HandleType>
    Handle<HandleType>& Handle<HandleType>::operator=(Handle&& other) noexcept
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
    void Handle<HandleType>::Reset() noexcept
    {
        if (context && handle.Valid())
        {
            //TODO: Implement Destroy
            //EOS::Destroy(context, handle);
        }

        context = nullptr;
        handle = HandleType{};
    }

    template <typename HandleType>
    Handle<HandleType>::operator HandleType() const noexcept
    {
        return handle;
    }

    template <typename HandleType>
    bool Handle<HandleType>::Empty() const noexcept
    {
        return handle.Empty();
    }

    template <typename HandleType>
    HandleType Handle<HandleType>::Release() noexcept
    {
        return std::exchange(handle, HandleType{});
    }

    template <typename HandleType>
    auto Handle<HandleType>::Gen() const noexcept
    {
        return handle.Gen();
    }

    template <typename HandleType>
    auto Handle<HandleType>::Index() const noexcept
    {
        return handle.Index();
    }

    template <typename HandleType>
    auto Handle<HandleType>::IndexAsVoid() const noexcept
    {
        return handle.IndexAsVoid();
    }

    template <typename HandleType>
    IContext* Handle<HandleType>::Context() const noexcept
    {
        return context;
    }

    template <typename HandleType>
    const HandleType& Handle<HandleType>::Get() const noexcept
    {
        return handle;
    }
}