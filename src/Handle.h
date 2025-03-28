#pragma once

#include <concepts>
#include <utility>
#include <cstdint>

#include "defines.h"

namespace EOS
{
    //Forward Declaring
    class IContext;

    // Concept for required HandleType operations
    template<typename T>
    concept ValidHandle = requires(T t)
    {
        { t.Valid() } -> std::convertible_to<bool>;
        { t.Empty() } -> std::convertible_to<bool>;
        { t.Gen() } -> std::convertible_to<uint32_t>;
        { t.Index() } -> std::convertible_to<uint32_t>;
        { t.IndexAsVoid() } -> std::convertible_to<void*>;
    };

    template<typename HandleType>
    class Handle
    {
        public:
        static_assert(std::is_default_constructible_v<HandleType>, "HandleType must be default constructible");
        static_assert(ValidHandle<HandleType>, "HandleType doesn't satisfy ValidHandle concept");

        Handle() = default;
        Handle(IContext* ctx, HandleType hdl) noexcept;
        ~Handle() noexcept;

        DELETE_COPY(Handle)
        [[nodiscard]] Handle(Handle&& other) noexcept;
        [[nodiscard]] Handle& operator=(Handle&& other) noexcept;

        void Reset() noexcept;

        [[nodiscard]] explicit operator HandleType() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] HandleType Release() noexcept;
        [[nodiscard]] auto Gen() const noexcept;
        [[nodiscard]] auto Index() const noexcept;
        [[nodiscard]] auto IndexAsVoid() const noexcept;
        [[nodiscard]] IContext* Context() const noexcept;
        [[nodiscard]] const HandleType& Get() const noexcept;

    private:
        IContext* context{nullptr};
        HandleType handle{};
    };
}