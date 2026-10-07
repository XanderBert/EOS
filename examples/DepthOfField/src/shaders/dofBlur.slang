// Depth of field, 2/3: one direction of a separable 5 tap Gaussian blur, with the radius scaled by the circle of
// confusion. A graph runs it twice, once per direction.
import eos.core;
import eos.pass;

enum BlurDirection
{
    Horizontal,
    Vertical,
};

[Pass]
struct DofBlur
{
    [Input] DescriptorHandle<Texture2D> input;
    [Output] [Format("RGBA_F16")] [SizeOf("input")] [Bypass("input")] DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<SamplerState> linearSampler;

    BlurDirection direction = BlurDirection.Horizontal;
    [Range(0.0, 12.0)] float maxBlurRadius = 1.0;
};

[[vk::push_constant]] DofBlur pass;

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
    const float4 center = pass.input.SampleLevel(pass.linearSampler, uv, 0.0);
    const float coc = center.a;

    const float2 axis = pass.direction == BlurDirection.Horizontal ? float2(1.0, 0.0) : float2(0.0, 1.0);
    const float radius = pass.maxBlurRadius * coc;
    const float2 step = axis * (1.0 / float2(imageSize)) * max(radius, 1.0);

    float3 color = center.rgb * 0.227027;
    float weight = 0.227027;

    // Precomputed offsets and weights for a 5 tap Gaussian.
    const float offsets[2] = { 1.384615, 3.230769 };
    const float weights[2] = { 0.316216, 0.070270 };

    for (int i = 0; i < 2; ++i)
    {
        const float2 offset = offsets[i] * step;
        color += pass.input.SampleLevel(pass.linearSampler, uv + offset, 0.0).rgb * weights[i];
        color += pass.input.SampleLevel(pass.linearSampler, uv - offset, 0.0).rgb * weights[i];
        weight += 2.0 * weights[i];
    }

    outputImage[pixelCoord] = float4(color / weight, coc);
}
