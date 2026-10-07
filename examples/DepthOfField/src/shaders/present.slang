// Copies a texture to the screen.
import eos.core;
import eos.fullscreen;
import eos.pass;

[Pass]
struct Present
{
    [Input] DescriptorHandle<Texture2D> input;
    DescriptorHandle<SamplerState> linearSampler;
};

[[vk::push_constant]] Present pass;

struct Targets
{
    [Output] [Clear(0.02, 0.02, 0.02, 1.0)] float4 output : SV_Target0;
};

[shader("fragment")]
Targets fragmentMain(FullscreenVaryings input)
{
    float2 uv = input.uv;
    uv.y = 1.0 - uv.y;

    Targets targets;
    targets.output = pass.input.Sample(pass.linearSampler, uv);
    return targets;
}
