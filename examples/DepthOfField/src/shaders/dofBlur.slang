public struct DofBlurPC
{
    public DescriptorHandle<Texture2D>           inputImage;
    public DescriptorHandle<RWTexture2D<float4>> outputImage;
    public DescriptorHandle<SamplerState>        samplerState;
    public float maxBlurRadius;
    public float texelSizeX;
    public float texelSizeY;
};

[[vk::push_constant]] DofBlurPC pc;

// One pass of a separable 5 tap Gaussian blur along axis, with the radius scaled by the circle of confusion.
void Blur(uint2 pixelCoord, float2 axis)
{
    const RWTexture2D<float4> outputImage = pc.outputImage;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);
    if (any(pixelCoord >= imageSize)) return;

    const float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);
    const float4 center = pc.inputImage.SampleLevel(pc.samplerState, uv, 0.0);
    const float coc = center.a;

    const float radius = pc.maxBlurRadius * coc;
    const float2 step = axis * float2(pc.texelSizeX, pc.texelSizeY) * max(radius, 1.0);

    float3 color = center.rgb * 0.227027;
    float weight = 0.227027;

    // Precomputed offsets and weights for a 5 tap Gaussian.
    const float offsets[2] = { 1.384615, 3.230769 };
    const float weights[2] = { 0.316216, 0.070270 };

    for (int i = 0; i < 2; ++i)
    {
        const float2 offset = offsets[i] * step;
        color += pc.inputImage.SampleLevel(pc.samplerState, uv + offset, 0.0).rgb * weights[i];
        color += pc.inputImage.SampleLevel(pc.samplerState, uv - offset, 0.0).rgb * weights[i];
        weight += 2.0 * weights[i];
    }

    outputImage[pixelCoord] = float4(color / weight, coc);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void blurHorizontal(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Blur(dispatchThreadID.xy, float2(1.0, 0.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void blurVertical(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Blur(dispatchThreadID.xy, float2(0.0, 1.0));
}
