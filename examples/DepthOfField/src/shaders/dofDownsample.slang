// Depth of field, 1/3: the scene at half resolution, with the circle of confusion in alpha.
import eos.core;
import eos.pass;
import dofCommon;

[Pass]
struct DofDownsample
{
    [Input] DescriptorHandle<Texture2D> color;
    [Input] DescriptorHandle<Texture2D> worldPos;
    [Output] [Format("RGBA_F16")] [Scale(0.5)] [SizeOf("color")] DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<SamplerState> linearSampler;

    [Range(0.1, 25.0)] float focusDistance = 6.0;
    [Range(0.1, 20.0)] float focusRange = 3.0;
};

[[vk::push_constant]] DofDownsample pass;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const RWTexture2D<float4> outputImage = pass.output;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);

    const uint2 pixelCoord = dispatchThreadID.xy;
    if (any(pixelCoord >= imageSize)) return;

    const float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);
    // Source is full-res, output is half-res.
    const float2 texel = 1.0 / float2(imageSize * 2);

    float3 color = float3(0.0);
    float coc = 0.0;

    // 2x2 box downsample; keep max CoC to preserve blur intent.
    const float2 offsets[4] = { float2(-0.25, -0.25), float2(0.25, -0.25), float2(-0.25, 0.25), float2(0.25, 0.25) };
    for (int i = 0; i < 4; ++i)
    {
        const float2 sampleUV = uv + offsets[i] * texel;
        const float3 worldPos = pass.worldPos.SampleLevel(pass.linearSampler, sampleUV, 0.0).xyz;
        coc = max(coc, ComputeCoC(length(worldPos), pass.focusDistance, pass.focusRange));
        color += pass.color.SampleLevel(pass.linearSampler, sampleUV, 0.0).rgb;
    }

    outputImage[pixelCoord] = float4(color * 0.25, coc);
}
