#pragma once
#include <concepts>
#include <cstdint>
#include <utility>


#define DELETE_COPY(ClassName)                   \
ClassName(const ClassName&) = delete;            \
ClassName& operator=(const ClassName&) = delete;

#define DELETE_MOVE(ClassName)                   \
ClassName (ClassName&&) = delete;                \
ClassName& operator=(ClassName&&) = delete;

#define DELETE_COPY_MOVE(ClassName)              \
DELETE_COPY(ClassName)                           \
DELETE_MOVE(ClassName)


namespace EOS
{
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
    class Holder final
    {
    public:
        // Constructor and Function constraints
        static_assert(std::is_default_constructible_v<HandleType>, "HandleType must be default constructible");
        static_assert(ValidHandle<HandleType>, "HandleType doesn't satisfy ValidHandle concept");

        Holder() = default;
        Holder(IContext* ctx, HandleType hdl) noexcept
        : context{ctx}
        , handle{std::move(hdl)}
        {}

        ~Holder() noexcept
        {
            Reset();
        }

        DELETE_COPY(Holder)

        Holder(Holder&& other) noexcept
        : context{std::exchange(other.context, nullptr)}
        , handle{std::exchange(other.handle, HandleType{})}
        {}

        Holder& operator=(Holder&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                context = std::exchange(other.context, nullptr);
                handle = std::exchange(other.handle, HandleType{});
            }

            return *this;
        }

        Holder& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        // Explicit conversion operator
        [[nodiscard]] explicit operator HandleType() const noexcept
        {
            return handle;
        }

        // Observers
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return handle.Valid();
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return handle.Empty();
        }

        void Reset() noexcept
        {
            if (context && handle.Valid())
            {
                //EOS::Destroy(context, handle);
            }

            context = nullptr;
            handle = HandleType{};
        }

        [[nodiscard]] HandleType Release() noexcept
        {
            return std::exchange(handle, HandleType{});
        }

        // Accessors
        [[nodiscard]] auto Gen() const noexcept { return handle.Gen(); }
        [[nodiscard]] auto Index() const noexcept { return handle.Index(); }
        [[nodiscard]] auto IndexAsVoid() const noexcept { return handle.IndexAsVoid(); }

        // Context access
        [[nodiscard]] EOS::IContext* Context() const noexcept { return context; }

        // Handle access
        [[nodiscard]] const HandleType& Get() const noexcept { return handle; }

    private:
        IContext* context{nullptr};
        HandleType handle{};
    };




    class ICommandBuffer
    {
    protected:
        ICommandBuffer() = default;

    public:
        DELETE_COPY_MOVE(ICommandBuffer);
        virtual ~ICommandBuffer() = default;

    };

    class IContext
    {
    protected:
        IContext() = default;

    public:
        DELETE_COPY_MOVE(IContext);
        virtual ~IContext() = default;

        [[nodiscard]] virtual ICommandBuffer& AcquireCommandBuffer() = 0;
    };
}