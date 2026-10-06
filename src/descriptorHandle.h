#pragma once

#include <cstdint>

#include "handle.h"

// Kept out of EOS.h so headers generated from Slang (src/generated, see EOSShaderCompilerTool) can use it without
// including the whole engine API.
namespace EOS
{
    /**
     * @brief A bindless reference to a texture, sampler or acceleration structure: the C++ side of Slang's
     *        DescriptorHandle<T> (see src/shaders/eos/bindless.slang). Put it in push constants or GPU buffers; shaders
     *        use it like the resource itself, e.g. albedo.Sample(linearSampler, uv).
     *        A default-constructed handle refers to nothing, which shaders can test with IsValid(handle).
     */
    struct DescriptorHandle final
    {
        static constexpr uint32_t InvalidIndex = 0xFFFFFFFF;

        uint32_t Index = InvalidIndex;  // index into the bindless array of the resource's kind
        uint32_t Reserved = 0;          // Slang's DescriptorHandle is a uint2; EOS only uses the first component

        constexpr DescriptorHandle() = default;
        constexpr explicit DescriptorHandle(uint32_t index) : Index(index) {}

        DescriptorHandle(const TextureHandle& texture) : Index(ToIndex(texture)) {}
        DescriptorHandle(const SamplerHandle& sampler) : Index(ToIndex(sampler)) {}
        DescriptorHandle(const AccelStructHandle& accelerationStructure) : Index(ToIndex(accelerationStructure)) {}
        // Defined in EOS.h, where Holder is complete.
        inline DescriptorHandle(const TextureHolder& texture);
        inline DescriptorHandle(const SamplerHolder& sampler);
        inline DescriptorHandle(const AccelStructHolder& accelerationStructure);

        [[nodiscard]] bool Valid() const { return Index != InvalidIndex; }

    private:
        template<typename HandleType>
        [[nodiscard]] static uint32_t ToIndex(const HandleType& handle) { return handle.Valid() ? handle.Index() : InvalidIndex; }
    };
    static_assert(sizeof(DescriptorHandle) == 8, "Must match the size of Slang's DescriptorHandle<T> (a uint2)");
}
