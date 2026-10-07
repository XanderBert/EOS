// Depth of field, 3/3: blends the sharp scene with the blurred one by the circle of confusion. Disabled, it passes the
// sharp scene on.
import eos.core;
import eos.pass;
import dofCommon;

[Pass]
struct DofComposite
{
    [Input] DescriptorHandle<Texture2D> color;
    [Input] DescriptorHandle<Texture2D> blurred;
    [Input] DescriptorHandle<Texture2D> worldPos;
    [Output] [Bypass("color")] DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<SamplerState> linearSampler;

    [Range(0.1, 25.0)] float focusDistance = 6.0;
    [Range(0.1, 20.0)] float focusRange = 3.0;
};

[[vk::push_constant]] DofComposite pass;

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

    const float3 worldPos = pass.worldPos.SampleLevel(pass.linearSampler, uv, 0.0).xyz;
    const float coc = ComputeCoC(length(worldPos), pass.focusDistance, pass.focusRange);

    const float3 sharp = pass.color.SampleLevel(pass.linearSampler, uv, 0.0).rgb;
    const float3 blurred = pass.blurred.SampleLevel(pass.linearSampler, uv, 0.0).rgb;

    const float blend = smoothstep(0.2, 1.0, coc);
    outputImage[pixelCoord] = float4(lerp(sharp, blurred, blend), 1.0);
}
