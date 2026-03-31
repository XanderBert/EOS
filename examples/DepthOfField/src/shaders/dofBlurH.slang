import common;

public struct DofBlurPC
{
    public uint  inputImageID;
    public uint  outputImageID;
    public uint  samplerID;
    public float maxBlurRadius;
    public float texelSizeX;
    public float texelSizeY;
    public float pad0;
    public float pad1;
};

[[vk::push_constant]] DofBlurPC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    int2 imageSize = ImageBindless2DSize(pc.outputImageID);

    if (pixelCoord.x >= imageSize.x || pixelCoord.y >= imageSize.y)
    {
        return;
    }

    float2 uv = (float2(pixelCoord) + float2(0.5, 0.5)) / float2(imageSize);
    float4 center = TextureBindless2D(pc.inputImageID, pc.samplerID, uv);
    float coc = center.a;

    // Separable Gaussian blur with CoC-scaled radius.
    float radius = pc.maxBlurRadius * coc;
    float2 step = float2(pc.texelSizeX, 0.0) * max(radius, 1.0);

    float3 color = center.rgb * 0.227027;
    float weight = 0.227027;

    // Precomputed weights for a 5 tap Gaussian.
    float2 offsets[4] = { float2(1.384615, 0.0), float2(-1.384615, 0.0), float2(3.230769, 0.0), float2(-3.230769, 0.0) };
    float weights[2] = { 0.316216, 0.070270 };

    for (int i = 0; i < 2; ++i)
    {
        float2 off = offsets[i * 2] * step;
        float2 offN = offsets[i * 2 + 1] * step;
        color += TextureBindless2D(pc.inputImageID, pc.samplerID, uv + off).rgb * weights[i];
        color += TextureBindless2D(pc.inputImageID, pc.samplerID, uv + offN).rgb * weights[i];
        weight += 2.0 * weights[i];
    }

    color /= weight;
    ImageBindless2DStore(pc.outputImageID, int2(pixelCoord), float4(color, coc));
}
