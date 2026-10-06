import eos.core;
import dofCommon;

[CppExport]
public struct DofCompositePC
{
    public DescriptorHandle<Texture2D>           sceneColor;
    public DescriptorHandle<Texture2D>           blurred;
    public DescriptorHandle<Texture2D>           worldPos;
    public DescriptorHandle<SamplerState>        samplerState;
    public DescriptorHandle<RWTexture2D<float4>> outputImage;
    public float focusDistance;
    public float focusRange;
    public float maxBlurRadius;
};

[[vk::push_constant]] DofCompositePC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const RWTexture2D<float4> outputImage = pc.outputImage;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);

    const uint2 pixelCoord = dispatchThreadID.xy;
    if (any(pixelCoord >= imageSize)) return;

    const float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);

    const float3 worldPos = pc.worldPos.SampleLevel(pc.samplerState, uv, 0.0).xyz;
    const float coc = ComputeCoC(length(worldPos), pc.focusDistance, pc.focusRange);

    const float3 sharp = pc.sceneColor.SampleLevel(pc.samplerState, uv, 0.0).rgb;
    const float3 blurred = pc.blurred.SampleLevel(pc.samplerState, uv, 0.0).rgb;

    const float blend = smoothstep(0.2, 1.0, coc);
    outputImage[pixelCoord] = float4(lerp(sharp, blurred, blend), 1.0);
}
