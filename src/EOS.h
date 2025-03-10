#pragma once
#include <concepts>
#include <cstdint>
#include <utility>
#include <string_view>


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

    enum class HardwareDeviceType
    {
        Discrete = 0,
        Integrated = 1
    };

    struct HardwareDeviceDescription
    {
        uintptr_t id{};
        HardwareDeviceType type { HardwareDeviceType::Integrated} ;
        std::string_view name[256]{};
    };

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
        static_assert(std::is_default_constructible_v<HandleType>, "HandleType must be default constructible");
        static_assert(ValidHandle<HandleType>, "HandleType doesn't satisfy ValidHandle concept");

        Holder() = default;
        Holder(IContext* ctx, HandleType hdl) noexcept;
        ~Holder() noexcept;

        DELETE_COPY(Holder)
        Holder(Holder&& other) noexcept;
        Holder& operator=(Holder&& other) noexcept;

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