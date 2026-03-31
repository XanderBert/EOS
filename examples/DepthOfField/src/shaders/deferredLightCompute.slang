import common;
import pbrShared;

public struct DeferredLightingPC
{
    public uint   gbufferAlbedoID;
    public uint   gbufferNormalID;
    public uint   gbufferWorldPosID;
    public uint   samplerID;
    public uint   outputImageID;
    public uint   debugView;
    public uint   pad0;
    public uint   pad1;
    public float4 cameraPos;
    public float4 lightDirIntensity;
    public float4 lightColorAmbient;
};

[[vk::push_constant]] DeferredLightingPC pc;

float3 DecodeNormal(float3 packedNormal)
{
    return normalize(packedNormal * 2.0 - 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    int2 imageSize = ImageBindless2DSize(pc.outputImageID);
    float2 uv = (float2(pixelCoord) + float2(0.5, 0.5)) / float2(imageSize);

    float4 albedoMetallic = TextureBindless2D(pc.gbufferAlbedoID, pc.samplerID, uv);
    float4 normalRoughness = TextureBindless2D(pc.gbufferNormalID, pc.samplerID, uv);
    float3 worldPos = TextureBindless2D(pc.gbufferWorldPosID, pc.samplerID, uv).xyz;

    float3 albedo = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    float3 normal = DecodeNormal(normalRoughness.rgb);
    float roughness = saturate(normalRoughness.a);

    float4 result;

    if (pc.debugView == 1) 
    {
        result = float4(albedo, 1.0);
    }
    else if (pc.debugView == 2) 
    {
        result = float4(normal * 0.5 + 0.5, 1.0);
    }
    else if (pc.debugView == 3) 
    {
        result = roughness.xxxx;
    }
    else if (pc.debugView == 4) 
    {
        result = float4(abs(worldPos) * 0.05, 1.0);
    }
    else
    {
        Material material;
        material.albedo = albedo;
        material.metallic = metallic;
        material.roughness = roughness;

        PBRSurface surface;
        surface.material = material;
        surface.normal = normal;
        surface.viewDir = normalize(pc.cameraPos.xyz - worldPos);
        surface.lightDir = normalize(-pc.lightDirIntensity.xyz);

        float3 color = EvaluatePBR(surface);
        color *= pc.lightColorAmbient.rgb * pc.lightDirIntensity.w;

        result = float4(color, 1.0);
    }

    ImageBindless2DStore(pc.outputImageID, int2(pixelCoord), result);
}
