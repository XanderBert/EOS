#pragma once
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "window.h"

#define DELETE_COPY(ClassName)                   \
ClassName(const ClassName&) = delete;            \
ClassName& operator=(const ClassName&) = delete;

#define DELETE_MOVE(ClassName)                   \
ClassName (ClassName&&) = delete;                \
ClassName& operator=(ClassName&&) = delete;

#define DELETE_COPY_MOVE(ClassName)              \
DELETE_COPY(ClassName)                           \
DELETE_MOVE(ClassName)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

namespace EOS
{
    class IContext;

    enum class HardwareDeviceType
    {
        Integrated  = 1,
        Discrete    = 2,
        Virtual     = 3,
        Software    = 4
    };

    struct HardwareDeviceDescription
    {
        uintptr_t id{};
        HardwareDeviceType type { HardwareDeviceType::Integrated} ;
        std::string name{};
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

    struct ContextConfiguration final
    {
        bool enableValidationLayers{ true };
    };

    class IContext
    {
    protected:
        IContext() = default;

    public:
        DELETE_COPY_MOVE(IContext);
        virtual ~IContext() = default;
    };

    struct ContextCreationDescription final
    {
        ContextConfiguration    config;
        GLFWwindow*             window;
        void*                   display;
        HardwareDeviceType      preferredHardwareType;
    };

    std::unique_ptr<IContext> CreateContextWithSwapchain(const ContextCreationDescription& contextCreationDescription);
}