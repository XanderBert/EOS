[![Windows & Linux Build Test](https://github.com/XanderBert/EOS/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/XanderBert/EOS/actions/workflows/cmake-multi-platform.yml)

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
> Dependencies are fetched automatically during CMake configure using `FetchContent` in `cmake/deps.cmake`.
> Versions are centralized in `cmake/deps-lock.cmake`.

### Core EOS dependencies
- [GLFW](https://github.com/glfw/glfw)
- [Volk](https://github.com/zeux/volk)
- [Vulkan Utility Libraries](https://github.com/KhronosGroup/Vulkan-Utility-Libraries)
- [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [spdlog](https://github.com/gabime/spdlog)
- [KTX-Software](https://github.com/KhronosGroup/KTX-Software)
- [GLM](https://github.com/g-truc/glm) 
### Optional EOS dependencies
- [Slang](https://github.com/shader-slang/slang) when `EOS_SHADER_TOOLS=ON`
- [stb](https://github.com/nothings/stb) when `EOS_BUILD_TEXTURE_TOOLS=ON`
- [Dear ImGui](https://github.com/ocornut/imgui) when `EOS_USE_IMGUI=ON`
- [Tracy](https://github.com/wolfpld/tracy) when `EOS_USE_TRACY=ON`

### Example-only dependencies
- [fastGLTF](https://github.com/spnda/fastgltf)

### System dependencies
- Vulkan SDK (required)


# CMake Options
All build toggles are exposed as CMake options and can be configured with `-D...` flags.

```cmake
option(EOS_VULKAN "Enable Vulkan backend" ON)
option(EOS_USE_IMGUI "Enable ImGui integration" ON)
option(EOS_USE_TRACY "Enable Tracy profiler" ON)
option(EOS_BUILD_EXAMPLES "Build example applications" ON)
option(EOS_SHADER_TOOLS "Enable shader tools (hot reload, shader compiler, prebuild shader compile step)" ON)
option(EOS_BUILD_TEXTURE_TOOLS "Build the texture compressor tool" ON)
```

### Shader path cache variables

- `EOS_PROJECT_SHADER_PATH`:
    - Empty by default.
- `EOS_SHADER_OUTPUT_PATH`:
    - Defaults to `bin` in the repository root.
    - Used as the output location for compiled shaders.


# Shaders
Shaders are written in [Slang](https://shader-slang.org). Every `.slang` file with `[shader("...")]` entry points is a
program; the other files are modules it imports.

- **Compilation**: before an example builds, `EOSShaderCompilerTool` compiles its programs and the engine's into
  `bin/shaders/<Example>/<Debug|Release>/<module>.EOS`. Each file holds the SPIR-V of every entry point plus the
  reflection (push-constant size, thread-group size, specialization constants, vertex inputs, color outputs, bindings).
  A program is only recompiled when one of its source files (imports included), the compiler options or the Slang
  version changed. `--force` recompiles everything, `--reflect <module>` prints a program's reflection and
  `--dump-spirv <dir>` writes its SPIR-V. A build where no shader changed does not start Slang.
- **Precompiled library**: before EOS builds, the engine's modules (`src/shaders/eos/`) are precompiled to Slang IR
  in `bin/shaders/EOS/<Debug|Release>/modules/`. Every compile, at build time and at runtime, loads those instead of
  parsing and type-checking the library again, which makes recompiling a program that uses the material and BSDF
  code about three times faster (hot reload, shader graph edits). A module whose sources changed is compiled from
  source until the next build precompiles it again. Library modules are shared by all programs, so they must not
  depend on a program's `Defines`; vary them with generics, specialization constants or link-time constants.
- **Runtime**: `IContext::CreateShaderProgram({.Module = "shade"})` loads a program from the cache and falls back to
  compiling it when the cache is stale (only with `EOS_SHADER_TOOLS=ON`). Pipelines pick its entry points by name:
  `.VertexShader = {shade, "vertexMain"}`. `IContext::GetShaderProgram(handle)` returns the reflection, and
  `cmdDispatchThreads` uses the reflected `[numthreads]` to size a dispatch.
- **Pipeline layout**: every pipeline shares one layout: the bindless descriptor set plus one push-constant range
  visible to all stages (`IContext::GetMaxPushConstantSize()`, 256 bytes on desktop GPUs). Programs are checked
  against it when they are created: larger push constants or bindings outside the bindless set are rejected with an
  error. Pipeline creation also checks that every vertex shader input has a matching attribute.
- **Hot reload**: `IContext::ReloadShaders()` recompiles every program in use whose source files, imports included,
  changed on disk, and rebuilds the pipelines that use it. A program that fails to compile or validate keeps its
  previous version.
- **Conventions**: matrices are row-major and multiply row vectors (`mul(v, M)`), which matches glm's memory layout;
  buffers use scalar layout. Debug builds compile shaders with debug info and without optimization, so they can be
  stepped through in RenderDoc or Nsight.

## Structs shared with C++
Data that C++ writes and shaders read (buffer contents, push constants) is declared once, in Slang, and marked
`[CppExport]` (from `eos.core`). `EOSShaderCompilerTool` generates the C++ struct into `.generated/<module>.h` next to
the code that uses it: `src/.generated/` for the engine's modules (namespace `EOS`), `<Example>/src/.generated/` for an
example's (global namespace). Include it as `#include ".generated/shadowCommon.h"`.

```slang
import eos.core;

[CppExport]
public struct DofBlurPC
{
    public DescriptorHandle<Texture2D>           inputImage;
    public DescriptorHandle<RWTexture2D<float4>> outputImage;
    public float maxBlurRadius = 4.0;
}
```

The generated struct has the Slang field names and a `static_assert` for its size and every offset, so a layout
mismatch fails the C++ build instead of reaching the GPU. Types map to `glm` (`float3` to `glm::vec3`, `float4x4` to
`glm::mat4`), `T[N]` to `std::array`, pointers to `uint64_t` device addresses and `DescriptorHandle<T>` to
`EOS::DescriptorHandle`. Every enum of a module that exports structs is exported too (Slang does not allow attributes
on enums). Fields start zeroed, or at their Slang default when that is a literal number. `bool`, `half`, resources and
defaults that are not literal numbers are reported as errors with the reason; see `tools/ShaderTools/shaderCodegen.h`.

Headers are only rewritten when their content changes, so C++ is not rebuilt for shader edits that do not touch a
shared struct. They are build output and not committed (`.generated/` is in `.gitignore`), so they appear with the
first build; a build with `EOS_SHADER_TOOLS=OFF` cannot write them and needs an earlier build of the same checkout
with the tools on.

## Shader library
The engine's modules live in `src/shaders/eos/` and are imported as `eos.<name>`:

| Module | Contents |
|---|---|
| `eos.bindless` | `DescriptorHandle<T>` routing onto the bindless descriptor set; `IsValid(handle)` |
| `eos.core`, `eos.math`, `eos.color` | constants, `[CppExport]`, math helpers, sRGB conversion and luminance |
| `eos.sampling` | PCG random numbers, hemisphere and GGX visible-normal sampling, MIS weights |
| `eos.brdf` | GGX distribution, height-correlated Smith masking-shadowing, Fresnel |
| `eos.bsdf` | `IBSDF` (`Eval`, `Sample`, `EvalPdf`, `Albedo`), `ShadingFrame` and the glTF 2.0 `StandardBSDF` |
| `eos.material` | `IMaterial`, `SurfaceData`, texture samplers for implicit and explicit mip levels, and the glTF 2.0 `StandardMaterial` |
| `eos.lighting` | directional and point lights, constant ambient light for any `IBSDF` |
| `eos.fullscreen`, `eos.debugDraw`, `eos.imgui` | fullscreen triangle, debug shapes, the ImGui program |

**Bindless resources**: textures, samplers, storage images and acceleration structures are referenced with
`DescriptorHandle<T>` in shaders and `EOS::DescriptorHandle` in C++, an 8-byte value built from a `TextureHandle`,
`SamplerHandle` or `AccelStructHandle`. Put it in push constants or buffers and use it like the resource itself:
`albedo.Sample(linearSampler, uv)`. Every texture type shares one index space, so a texture must be read as the type it
was created as (`Texture2D`, `Texture2DArray`, `TextureCube`, ...). Buffers are passed as device addresses and read
through pointers (`MyStruct*`). `eos.bindless` is linked into every program, so this works without importing it.

**Materials**: `StandardMaterial` evaluates a glTF 2.0 metallic-roughness material (`StandardMaterialData`, generated
as `EOS::StandardMaterialData` in C++) into a `StandardBSDF`, shading frame, emission, opacity and occlusion. Textures are
optional: an empty handle means the factor alone is used. The rasterizer evaluates the BSDF per light (`eos.lighting`);
a path tracer uses `Sample` and `EvalPdf` from the same BSDF.

Alpha-tested (glTF `MASK`) materials are cut with `PassesAlphaTest(EvaluateOpacity(...))`, which also has to run in
depth-only passes, otherwise the cut-away parts still write depth and cast shadows. A fragment shader that can `discard`
turns off early depth testing for the whole draw, so depth-only passes draw the opaque meshes with a fragment shader
that never discards and only the alpha-tested ones with one that does. `PartitionMeshesByAlphaTest` in the example
helpers orders the meshes so that is two indirect draws (see the shadow-mapping examples).

# Render graph
Frames are described with `EOS::RenderGraph` (`src/renderGraph.h`), rebuilt every frame like Frostbite's FrameGraph
and Unreal's RDG. A pass declares what it renders to, samples, writes and reads; `Execute()` derives the rest.

```cpp
EOS::RenderGraph& graph = *App.Graph;
const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
const EOS::GraphTexture albedo = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_UN8, .DebugName = "Albedo"});
const EOS::GraphTexture depth  = graph.CreateTexture({.TextureFormat = EOS::Format::Z_F32, .DebugName = "Depth"});
const EOS::GraphTexture lit    = graph.CreateTexture({.TextureFormat = EOS::Format::RGBA_F16, .Scale = 0.5f, .DebugName = "Lit"});

graph.AddRasterPass("GBuffer").Color(EOS::Clear(albedo)).Depth(EOS::ClearDepth(depth)).Execute([&](EOS::PassContext& pass)
{
    cmdBindRenderPipeline(pass.Cmd, gbufferPipeline);
    cmdDrawIndexedIndirect(pass.Cmd, indirectBuffer, 0, drawCount);
});

graph.AddComputePass("Lighting").Sample(albedo).Write(lit).Execute([&](EOS::PassContext& pass)
{
    cmdBindComputePipeline(pass.Cmd, lightingPipeline);
    cmdPushConstants(pass.Cmd, LightingPC{.albedo = pass.Descriptor(albedo), .output = pass.Descriptor(lit)});
    cmdDispatchThreads(pass.Cmd, pass.Size(lit));
});

graph.Execute();   // barriers, render passes, markers, submit and present
```

- **Resources**: `CreateTexture`/`CreateBuffer` resources belong to the graph. Textures are sized relative to the
  swapchain (`Scale`) or fixed (`Size`), get their usage flags from how passes use them, are pooled across frames and
  are recreated when the window is resized. Their contents do not survive the frame; `CreateHistoryTexture` gives a
  texture that does, together with last frame's version (temporal effects).
- **Memory aliasing**: `CreateTexture` textures live in memory heaps that the graph keeps across frames. A texture is
  alive from the first pass that uses it to the last one, and textures that are never alive at the same time share
  memory (the depth of field example needs 73 MiB of textures and fits them in 42 MiB). The first pass that uses a
  texture discards its old contents and waits for the earlier users of that memory, this frame's or a previous one's.
  A heap grows when a frame needs more and shrinks when it stays more than twice as large as needed for 120 frames,
  for example after the window got smaller. `GetMemoryStatistics()` returns the texture bytes and the heap bytes;
  `SetAliasing(false)` gives every texture its own memory, which helps to tell an aliasing bug from another one.
  Heaps come from `IContext::CreateMemoryHeap`, and `TextureDescription::Heap`/`HeapOffset` place a texture in one.
- **Imports**: resources owned elsewhere are imported (`ImportTexture`, `ImportBuffer`, `ImportSwapchain`). The graph
  keeps the state it left them in for the next frame. Anything referenced before the graph executes, such as a texture
  the UI shows or a handle uploaded in a buffer, has to be such an import: graph resources only exist inside passes,
  where `pass.Descriptor()`, `pass.Texture()` and `pass.Address()` resolve them.
- **Per-frame data**: write it with `graph.AddUpload(name, buffer, data)`, which copies it on the GPU in order with the
  passes (`cmdUpdateBuffer`, at most 64 KiB), and let the passes that use it `.Read(buffer)`. Writing it from the CPU
  (`IContext::Upload` into host-visible memory) changes it while earlier frames may still be reading it: in the
  cascaded shadow mapping example that made early-Z and shading see different camera matrices, and the scene vanished
  while the camera moved.
- **Execution**: passes run in the order they were added. Passes whose results nothing uses are skipped (writes to
  imports, the swapchain and history textures always count). Every pass gets the barriers it needs in one batch,
  raster passes are wrapped in `cmdBeginRendering` for their targets, and every pass is a named debug marker.
- **Data**: the graph is flat arrays of resources, passes and accesses indexed by the handles, and pass functions live
  in a reused arena, so building a frame allocates nothing once the graph is warm.

In the examples, `App.AddUIPass(target, [&] { ... })` declares the frame's UI and draws it on top of `target`.

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


# Screenshots
![Shadow Mapping](assets/ShadowMapping.png)



# Inspiration
This project is **heavily** inspired by LVK and The Forge. 
It's created for my personal use to learn about modern Graphics Techniques
