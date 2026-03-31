import common;

public struct DofDownsamplePC
{
    public uint  sceneColorID;
    public uint  worldPosID;
    public uint  samplerID;
    public uint  outputImageID;
    public float focusDistance;
    public float focusRange;
    public float pad0;
    public float pad1;
};

[[vk::push_constant]] DofDownsamplePC pc;

float ComputeCoC(float dist)
{
    // Circle of confusion based on distance from focus plane.
    float coc = abs(dist - pc.focusDistance) / max(pc.focusRange, 0.0001);
    return saturate(coc);
}

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
    // Source is full-res, output is half-res.
    float2 texel = 1.0 / float2(imageSize * 2);

    float3 color = float3(0.0, 0.0, 0.0);
    float coc = 0.0;

    // 2x2 box downsample; keep max CoC to preserve blur intent.
    float2 offsets[4] = { float2(-0.25, -0.25), float2(0.25, -0.25), float2(-0.25, 0.25), float2(0.25, 0.25) };
    for (int i = 0; i < 4; ++i)
    {
        float2 sampleUV = uv + offsets[i] * texel;
        float3 worldPos = TextureBindless2D(pc.worldPosID, pc.samplerID, sampleUV).xyz;
        float dist = length(worldPos);
        coc = max(coc, ComputeCoC(dist));
        color += TextureBindless2D(pc.sceneColorID, pc.samplerID, sampleUV).rgb;
    }

    color *= 0.25;
    ImageBindless2DStore(pc.outputImageID, int2(pixelCoord), float4(color, coc));
}
