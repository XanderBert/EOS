# EOS
Eos the goddess of dawn, aka the first light of the day. A wordplay on lighting.

> [!WARNING] 
> This project is still in its early stages.

Eos aims to be:

- Bindless Rendering Framework. Mainly targetting Vulkan.
- As GPU-friendly as possible, while providing a "higher" level API for ease of use.
- only targets Windows and Linux.

# Dependencies
> [!NOTE] 
> These dependencies are used by EOS and are fetched automatically by CMake during the configuration step using FetchContent
>

 
- [GLFW](https://github.com/glfw/glfw)
- [VOLK](https://github.com/zeux/volk)
- [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [SPDLOG](https://github.com/gabime/spdlog)
- [SLANG](https://github.com/shader-slang/slang)
- [ASSIMP]()
- [GLM]()


# Building
This project is built using CMake and Ninja.


## **Prerequisites:**
1.  **CMake:** Ensure CMake is installed and accessible from your command line/terminal. ([Download CMake](https://cmake.org/download/))
2.  **Ninja:** Ensure Ninja is installed and accessible. ([Download Ninja](https://github.com/ninja-build/ninja/releases))
3.  **C++ Compiler:**
    * **Linux:** A C++ compiler like GCC or Clang. (e.g., `sudo apt update && sudo apt install build-essential g++` on Debian/Ubuntu).
    * **Windows:** Microsoft Visual C++ (MSVC), usually installed with Visual Studio. Make sure the "Desktop development with C++" workload is installed.
4.  **Vulkan SDK:** Install the Vulkan SDK for your platform. ([Download Vulkan SDK](https://vulkan.lunarg.com/sdk/home))


## **Build Steps:**

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/XanderBert/EOS.git
    cd EOS
    ```

2.  **Run the appropriate build script:**
    * Linux: `build.sh` script in the root directory of the project.
    * Windows: `build.bat` script in the root directory of the project.



# Future
Once this projects ages enough it will be converted to a separate Rendering library, and main loop will be moved to a separate project.



# Inspiration
This project is heavily inspired by LVK and The Forge


# Rules
- AvoidC-style Casts
- Use `[[nodiscard]]` whenever you return a value
- For each dependency a separate Fetch macro will be made in `FetchMacros.cmake` for ease of use and separation 
- Whenever possible `.reserve(n)` must be called on std::vector's and other dynamic array like objects
