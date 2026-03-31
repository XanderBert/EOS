import bindings;
import dofCommon;

public struct DofCompositePC
{
    public uint  sceneColorID;
    public uint  blurredID;
    public uint  worldPosID;
    public uint  samplerID;
    public uint  outputImageID;
    public float focusDistance;
    public float focusRange;
    public float maxBlurRadius;
};

[[vk::push_constant]] DofCompositePC pc;


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

    float3 worldPos = TextureBindless2D(pc.worldPosID, pc.samplerID, uv).xyz;
    float dist = length(worldPos);
    float coc = ComputeCoC(dist, pc.focusDistance, pc.focusRange);

    float3 sharp = TextureBindless2D(pc.sceneColorID, pc.samplerID, uv).rgb;
    float3 blurred = TextureBindless2D(pc.blurredID, pc.samplerID, uv).rgb;

    float blend = smoothstep(0.2, 1.0, coc);
    float3 finalColor = lerp(sharp, blurred, blend);

    ImageBindless2DStore(pc.outputImageID, int2(pixelCoord), float4(finalColor, 1.0));
}
