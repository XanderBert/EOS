import dofCommon;

public struct DofDownsamplePC
{
    public DescriptorHandle<Texture2D>           sceneColor;
    public DescriptorHandle<Texture2D>           worldPos;
    public DescriptorHandle<SamplerState>        samplerState;
    public DescriptorHandle<RWTexture2D<float4>> outputImage;
    public float focusDistance;
    public float focusRange;
};

[[vk::push_constant]] DofDownsamplePC pc;

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
    // Source is full-res, output is half-res.
    const float2 texel = 1.0 / float2(imageSize * 2);

    float3 color = float3(0.0);
    float coc = 0.0;

    // 2x2 box downsample; keep max CoC to preserve blur intent.
    const float2 offsets[4] = { float2(-0.25, -0.25), float2(0.25, -0.25), float2(-0.25, 0.25), float2(0.25, 0.25) };
    for (int i = 0; i < 4; ++i)
    {
        const float2 sampleUV = uv + offsets[i] * texel;
        const float3 worldPos = pc.worldPos.SampleLevel(pc.samplerState, sampleUV, 0.0).xyz;
        coc = max(coc, ComputeCoC(length(worldPos), pc.focusDistance, pc.focusRange));
        color += pc.sceneColor.SampleLevel(pc.samplerState, sampleUV, 0.0).rgb;
    }

    outputImage[pixelCoord] = float4(color * 0.25, coc);
}
