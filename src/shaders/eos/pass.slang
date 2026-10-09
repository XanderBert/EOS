// Render graph passes written in Slang.
//
// A shader file whose push constants are a struct marked [Pass] is a pass: graph files use it by the file's module
// name (`shader: dofComposite`), and the engine creates its pipeline, fills its push constants and records it, so
// it needs no C++. One pass per file. Its fields say what the pass reads and writes (pins) and what it can be tuned
// with (properties):
//
//     import eos.pass;
//
//     [Pass]
//     struct Composite
//     {
//         [Input] DescriptorHandle<Texture2D> color;                       // a texture other passes write
//         [Output] [Format("RGBA_F16")] DescriptorHandle<RWTexture2D<float4>> output;
//         DescriptorHandle<SamplerState> linearSampler;                     // filled with a linear sampler
//         [Range(0.0, 10.0)] float strength = 1.0;                          // a property
//         Tonemapper tonemapper;                                            // an enum: a choice by case name
//     };
//     [[vk::push_constant]] Composite pass;
//
//     [shader("compute")] [numthreads(8, 8, 1)] void computeMain(uint3 id : SV_DispatchThreadID) { ... }
//
// - Pins: DescriptorHandle<Texture*> fields are inputs ([Input] may be left out); DescriptorHandle<RWTexture*> fields
//   and pointers (T*) say [Input], [Output] or [InOut]. An output is created by the graph: [Format] (default: the
//   swapchain's), [Scale] (of the window, or of [SizeOf]'s texture), [Size] (fixed), [Layers] (an array texture) and
//   [Bypass] (what readers get while the pass is disabled) shape it. A pointer output is a buffer the size of what it
//   points to. A Scene* (eos.scene) is the scene a gltfScene node loads.
// - Depth: a DepthTarget field is the pass's depth attachment and takes no push-constant space: [Output] clears and
//   writes it, [InOut] loads and writes it, [Input] only tests against it. [DepthTest] gives the comparison (default:
//   Less, LessEqual for an input). A DescriptorHandle<Texture2D> input marked [DepthTest] is attached read-only and
//   can be sampled by the same pass (soft particles, decals); a pass that only samples depth (SSAO) takes a plain
//   texture input.
// - Samplers: DescriptorHandle<SamplerState> fields get a sampler of [Sampler]'s description, linear and repeating
//   without one.
// - Properties: bool, int, uint, float, float2-4 and enum fields. Graph files set them by field name; [Range] gives a
//   UI its slider range, and the field's default (scalars only) is the value until a graph file sets one.
// - A compute pass (a "compute" entry point) dispatches one thread per pixel of its first storage texture output, of the
//   texture pin [DispatchSizeOf] names, or [DispatchThreads] threads (attributes of the [Pass] struct). A raster pass
//   (a "fragment" entry point) renders into its color targets: the fields of the fragment shader's output marked
//   [Output] (cleared, [Clear] gives the color) or [InOut] (loaded). It draws a fullscreen triangle with eos.fullscreen's
//   vertex shader, whose FullscreenVaryings the fragment shader takes, or 3 vertices with a "vertex" entry point of its
//   own, or the scene with [DrawScene] (eos.scene). [Cull] and [DepthClamp] on the [Pass] struct set the rasterizer.
module "eos/pass";

[__AttributeUsage(_AttributeTargets.Struct)] public struct PassAttribute {};

// Compute passes: one thread per pixel of a texture pin, or a fixed number of threads (a buffer-only pass).
[__AttributeUsage(_AttributeTargets.Struct)] public struct DispatchSizeOfAttribute { string pin; };
[__AttributeUsage(_AttributeTargets.Struct)] public struct DispatchThreadsAttribute { int x; int y; int z; };

// Directions of pins.
[__AttributeUsage(_AttributeTargets.Var)] public struct InputAttribute {};
[__AttributeUsage(_AttributeTargets.Var)] public struct OutputAttribute {};
[__AttributeUsage(_AttributeTargets.Var)] public struct InOutAttribute {};

// An input that may stay unconnected; it then holds an invalid handle (check with IsValid) or a null pointer.
[__AttributeUsage(_AttributeTargets.Var)] public struct OptionalAttribute {};

// Outputs: a format name of EOS::Format ("RGBA_F16", "Z_F32") or "swapchain".
[__AttributeUsage(_AttributeTargets.Var)] public struct FormatAttribute { string format; };
[__AttributeUsage(_AttributeTargets.Var)] public struct ScaleAttribute { float scale; };
[__AttributeUsage(_AttributeTargets.Var)] public struct SizeAttribute { int width; int height; };
[__AttributeUsage(_AttributeTargets.Var)] public struct LayersAttribute { int count; };
[__AttributeUsage(_AttributeTargets.Var)] public struct SizeOfAttribute { string pin; };
[__AttributeUsage(_AttributeTargets.Var)] public struct BypassAttribute { string pin; };
[__AttributeUsage(_AttributeTargets.Var)] public struct ClearAttribute { float r; float g; float b; float a; };

// The depth attachment of a raster pass. Takes no space in the push constants.
public struct DepthTarget {};

// EOS::CompareOp.
public enum CompareOp
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

// On a depth pin: how fragments are compared with the depth already there.
[__AttributeUsage(_AttributeTargets.Var)] public struct DepthTestAttribute { CompareOp compare; };

// EOS::CullMode.
public enum CullMode
{
    None,
    Front,
    Back,
};

// Raster passes: which faces are culled (default: none), and whether depth is clamped instead of clipped (shadow maps).
[__AttributeUsage(_AttributeTargets.Struct)] public struct CullAttribute { CullMode mode; };
[__AttributeUsage(_AttributeTargets.Struct)] public struct DepthClampAttribute {};

// Samplers.
public enum Filter
{
    Nearest,
    Linear,
};

public enum Address
{
    Repeat,
    ClampToEdge,
    ClampToBorder,
    MirrorRepeat,
};

[__AttributeUsage(_AttributeTargets.Var)] public struct SamplerAttribute { Filter filter; Address address; };

// Properties.
[__AttributeUsage(_AttributeTargets.Var)] public struct RangeAttribute { float minimum; float maximum; };
