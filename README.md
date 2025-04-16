# EOS
Eos the goddess of dawn, aka the first light of the day. A wordplay on lighting.

> [!WARNING] 
> This project is still in its early stages.

Eos aims to be:

- Bindless Rendering Framework. Mainly targetting Vulkan.
- As GPU-friendly as possible, while providing a high-level API for ease of use.
- only targets Windows and Linux.

# Dependencies
- [GLFW](https://github.com/glfw/glfw)
- [VOLK](https://github.com/zeux/volk)
- [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [SPDLOG](https://github.com/gabime/spdlog)

# Building
This project is being build with CMake


# Future
Once this projects ages enough it will be converted to a separate Rendering library, and main loop will be moved to a separate project.


# Inspiration
This project is heavily inspired by LVK and The Forge


# Rules
- AvoidC-style Casts
- Use [[nodiscard]] whenever you return a value
- For each dependency a separate Fetch macro will be made in FetchMacros.cmake for ease of use and separation 
- Whenever possible .reserve(n) must be called on std::vector's and other dynamic array like objects
