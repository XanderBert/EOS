#include "handle.h"

namespace EOS
{
    template<typename ObjectType>
    bool Handle<ObjectType>::operator==(const Handle<ObjectType> &other) const
    {
        return Idx == other.Idx && Generation == other.Generation;
    }

    template<typename ObjectType>
    bool Handle<ObjectType>::operator!=(const Handle<ObjectType> &other) const
    {
        return Idx != other.Idx || Generation != other.Generation;
    }

    template<typename ObjectType>
    Handle<ObjectType>::operator bool() const
    {
        return Generation != 0;
    }


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
            //TODO: Implement Destroy
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
}