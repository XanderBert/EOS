// Draws a texture of a render graph for the UI to show (EOS::UI::GraphFilePanel): a layer of it, its values remapped
// from [minimum, maximum] to [0, 1], in grey when it is a depth texture. Drawn with eos.fullscreen's vertex shader.
module "eos/preview";

import eos.core;
import eos.bindless;
import eos.fullscreen;

[CppExport]
public struct PreviewConstants
{
    public DescriptorHandle<Texture2D> texture;
    public DescriptorHandle<Texture2DArray> textureArray;  // read instead of texture when valid
    public DescriptorHandle<SamplerState> samplerState;
    public uint layer;
    public uint grey;                                       // shows the first channel in grey (depth)
    public float minimum;
    public float maximum;
};

[[vk::push_constant]] PreviewConstants preview;

[shader("fragment")]
float4 fragmentMain(FullscreenVaryings input) : SV_Target0
{
    // Images are upside down until Present flips them; show them the way Present does.
    const float2 uv = float2(input.uv.x, 1.0 - input.uv.y);

    float4 value = IsValid(preview.textureArray)
                 ? preview.textureArray.SampleLevel(preview.samplerState, float3(uv, float(preview.layer)), 0.0)
                 : preview.texture.SampleLevel(preview.samplerState, uv, 0.0);
    if (preview.grey != 0) value = value.rrrr;

    const float3 color = saturate((value.rgb - preview.minimum) / max(preview.maximum - preview.minimum, 1e-6));
    return float4(color, 1.0);
}
