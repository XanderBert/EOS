import eos.core;
import eos.math;
import eos.bsdf;
import eos.lighting;
import eos.fullscreen;

[CppExport]
public struct DeferredLightingPC
{
    public DescriptorHandle<Texture2D>    gbufferAlbedo;
    public DescriptorHandle<Texture2D>    gbufferNormal;
    public DescriptorHandle<Texture2D>    gbufferWorldPos;
    public DescriptorHandle<SamplerState> samplerState;
    public uint   debugView;
    public float4 cameraPos;
    public float4 lightDirIntensity;    // xyz: direction the light travels, w: intensity
    public float4 lightColorAmbient;    // rgb: light color, w: ambient radiance
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
float4 fragmentMain(VSOutput input) : SV_Target
{
    const float2 uv = input.UV;

    const float4 albedoMetallic = pc.gbufferAlbedo.Sample(pc.samplerState, uv);
    const float4 normalRoughness = pc.gbufferNormal.Sample(pc.samplerState, uv);
    const float3 worldPos = pc.gbufferWorldPos.Sample(pc.samplerState, uv).xyz;

    const float3 albedo = albedoMetallic.rgb;
    const float3 normal = UnpackNormalUnorm(normalRoughness.rgb);
    const float roughness = saturate(normalRoughness.a);

    if (pc.debugView == 1) return float4(albedo, 1.0);
    if (pc.debugView == 2) return float4(normal * 0.5 + 0.5, 1.0);
    if (pc.debugView == 3) return roughness.xxxx;
    if (pc.debugView == 4) return float4(abs(worldPos) * 0.05, 1.0);

    const StandardBSDF bsdf = StandardBSDF(albedo, albedoMetallic.a, roughness);
    const ShadingFrame frame = ShadingFrame.FromNormal(normal);
    const float3 viewDirection = normalize(pc.cameraPos.xyz - worldPos);

    DirectionalLight sun;
    sun.direction = normalize(pc.lightDirIntensity.xyz);
    sun.radiance = pc.lightColorAmbient.rgb * pc.lightDirIntensity.w;

    const float3 color = EvaluateDirectionalLight(bsdf, frame, viewDirection, sun)
                       + EvaluateConstantAmbient(bsdf, frame, viewDirection, pc.lightColorAmbient.www, 1.0);
    return float4(color, 1.0);
}
