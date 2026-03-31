import bindings;
import fullscreen;
import shading;

public struct DeferredLightingPC
{
    public uint   gbufferAlbedoID;
    public uint   gbufferNormalID;
    public uint   gbufferWorldPosID;
    public uint   samplerID;
    public uint   debugView;
    public uint   pad0;
    public uint   pad1;
    public uint   pad2;
    public float4 cameraPos;
    public float4 lightDirIntensity;
    public float4 lightColorAmbient;
};

[[vk::push_constant]] DeferredLightingPC pc;

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint id : SV_VertexID)
{
    VSOutput o;
    FullscreenTriangle(id, o.UV, o.Pos);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    float2 uv = input.UV;

    float4 albedoMetallic = TextureBindless2D(pc.gbufferAlbedoID, pc.samplerID, uv);
    float4 normalRoughness = TextureBindless2D(pc.gbufferNormalID, pc.samplerID, uv);
    float3 worldPos = TextureBindless2D(pc.gbufferWorldPosID, pc.samplerID, uv).xyz;

    float3 albedo = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    float3 normal = DecodeNormal(normalRoughness.rgb);
    float roughness = saturate(normalRoughness.a);

    if (pc.debugView == 1) return float4(albedo, 1.0);
    if (pc.debugView == 2) return float4(normal * 0.5 + 0.5, 1.0);
    if (pc.debugView == 3) return roughness.xxxx;
    if (pc.debugView == 4) return float4(abs(worldPos) * 0.05, 1.0);

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

    return float4(color, 1.0);
}
