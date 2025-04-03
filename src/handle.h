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
        {
            //TODO::remove include for this!
            EOS::Logger->warn("made copy of handle");
        }

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

        [[nodiscard]] inline void* indexAsVoid() const
        {
            return reinterpret_cast<void*>(static_cast<ptrdiff_t>(Idx));
        }

        bool operator==(const Handle<ObjectType>& other) const;
        bool operator!=(const Handle<ObjectType>& other) const;
        explicit operator bool() const;

    private:
        Handle(uint32_t index, uint32_t gen) : Idx(index), Generation(gen){};

        template<typename ObjectType_, typename ObjectType_Impl>
        friend class Pool;

        uint32_t Idx = 0;
        uint32_t Generation = 0;
    };
    static_assert(sizeof(Handle<class Foo>) == sizeof(uint64_t));




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
    class Holder
    {
    public:
        static_assert(std::is_default_constructible_v<HandleType>, "HandleType must be default constructible");
        static_assert(ValidHolder<HandleType>, "HandleType doesn't satisfy ValidHolder concept");

        Holder() = default;
        Holder(IContext* ctx, HandleType hdl) noexcept;
        ~Holder() noexcept;

        DELETE_COPY(Holder)
        [[nodiscard]] Holder(Holder&& other) noexcept;
        [[nodiscard]] Holder& operator=(Holder&& other) noexcept;

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
