# EOS
Eos the goddess of dawn, aka the first light of the day. a wordplay on lighting.

Eos aims to be:

Modern Bindless Vulkan Rendering Framework for C++20.
Aims to be as GPU-friendly as possible, while providing a high-level API for ease of use.
Will only target Windows and Linux.

# Dependencies
- Vulkan SDK
- CMake
- C++ compiler
- [GLFW]
- [GLM]
- [VOLK]
- [STB]
- [SLANG]


# Building
This project is being build with CMAKE



# Future
Once this projects ages enough it will be converted to a separate Rendering library, and main loop will be moved to a separate project.


# Inspiration
This project is heavily inspired by LVK and The Forge


# Rules
- No C-style Casts
- Use [[nodiscard]] whenever you return a value
- For each dependency a separate Fetch macro will be made in FetchMacros.cmake for ease of use and separation 
- Whenever possible call reserve on std::vector's