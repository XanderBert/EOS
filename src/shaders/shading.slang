module shading;
import bindings;
import math;

#define PBR_PI 3.14159265358979323846

public struct Material
{
    public float3 albedo;
    public float metallic;
    public float roughness;
}

public struct MaterialTextureSet
{
    public uint albedo;
    public uint normal;
    public uint metallicRoughness;
}

public struct PBRShadingInput
{
    public float3 worldPos;
    public float3 geometricNormal;
    public float3 tangent;
    public float2 uv;
    public float3 cameraPos;
    public float3 lightPos;
}

public struct PBRSurface
{
    public Material material;
    public float3 normal;
    public float3 viewDir;
    public float3 lightDir;
}

public float3 DecodeNormal(float3 packedNormal)
{
    return normalize(packedNormal * 2.0 - 1.0);
}

public float3 ReconstructNormal(float2 normalSample)
{
    float2 xy = 2.0 * normalSample - float2(1.0);
    float z = sqrtFast(clamp(1.0 - dot(xy, xy), 0.0, 1.0));
    return normalize(float3(xy, z));
}

public float3 GetShadingNormal(float3 n, float3 v, float2 normalSample, float2 uv, float3 tangent)
{
    float3 tangentNormal = ReconstructNormal(normalSample);
    float3 N = normalize(n);
    // Re-orthogonalize: the interpolated tangent drifts off perpendicular to the interpolated normal.
    float3 T = normalize(tangent - N * dot(N, tangent));
    float3 B = normalize(cross(N, T));

    float3x3 TBN = transpose(float3x3(T, B, N));

    return normalize(mul(TBN, tangentNormal));
}

public float DistributionGGX(float dotNH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PBR_PI * denom * denom);
}

public float GeometrySchlickSmithGGX(float dotNL, float dotNV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gl = dotNL / (dotNL * (1.0 - k) + k);
    float gv = dotNV / (dotNV * (1.0 - k) + k);
    return gl * gv;
}

public float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

public float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    return f0 + (max((1.0 - roughness).xxx, f0) - f0) * pow(1.0 - cosTheta, 5.0);
}

// Analytic fit of the split-sum environment BRDF LUT (Karis, "Physically Based Shading on Mobile").
// Returns (scale, bias) so that specular = f0 * scale + bias.
public float2 EnvBRDFApprox(float dotNV, float roughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

public Material SampleMaterial(MaterialTextureSet textures, uint samplerId, float2 uv)
{
    float3 albedoSample = TextureBindless2D(textures.albedo, samplerId, uv).rgb;
    float4 metallicRoughnessSample = TextureBindless2D(textures.metallicRoughness, samplerId, uv);

    Material material;
    material.albedo = albedoSample;
    material.metallic = metallicRoughnessSample.b;
    material.roughness = metallicRoughnessSample.g;
    return material;
}

public float3 SampleNormal(MaterialTextureSet textures, uint samplerId, float2 uv, float3 geometricNormal, float3 worldPos, float3 tangent)
{
    float2 normalSample = TextureBindless2D(textures.normal, samplerId, uv).rg;
    return GetShadingNormal(geometricNormal, worldPos, normalSample, uv, tangent);
}

public PBRSurface BuildPBRSurface(PBRShadingInput input, Material material, float3 shadingNormal)
{
    PBRSurface surface;
    surface.material = material;
    surface.normal = normalize(shadingNormal);
    surface.viewDir = normalize(input.cameraPos - input.worldPos);
    surface.lightDir = normalize(input.lightPos - input.worldPos);
    return surface;
}

public float3 EvaluatePBR(PBRSurface surface)
{
    float3 n = surface.normal;
    float3 v = surface.viewDir;
    float3 l = surface.lightDir;
    Material material = surface.material;

    float3 h = normalize(v + l);
    float dotNH = clamp(dot(n, h), 0.0, 1.0);
    float dotNV = clamp(dot(n, v), 0.0, 1.0);
    float dotNL = clamp(dot(n, l), 0.0, 1.0);
    float dotVH = clamp(dot(v, h), 0.0, 1.0);

    float3 f0 = lerp(float3(0.04), material.albedo, material.metallic);

    float d = DistributionGGX(dotNH, material.roughness);
    float g = GeometrySchlickSmithGGX(dotNL, dotNV, material.roughness);
    float3 f = FresnelSchlick(dotVH, f0); // microfacet Fresnel uses the half-vector angle, not N.V
    float3 spec = d * f * g / (4.0 * dotNL * dotNV + 0.001);

    float3 kd = (1.0 - f) * (1.0 - material.metallic);
    float3 lo = (kd * material.albedo / PBR_PI + spec) * dotNL;

    // Constant-environment stand-in for IBL. brdf = split-sum (scale, bias) applied as f0 * scale + bias.
    float2 brdf = EnvBRDFApprox(dotNV, material.roughness);
    float3 reflection = float3(0.3);
    float3 diffuse = 0.7 * material.albedo;
    float3 fr = FresnelSchlickRoughness(dotNV, f0, material.roughness);

    float3 ambientKd = (1.0 - fr) * (1.0 - material.metallic);
    float3 ambientSpecular = reflection * (f0 * brdf.x + brdf.y);
    float3 ambient = ambientKd * diffuse + ambientSpecular;

    return ambient + lo;
}

public float3 ShadePBR(PBRShadingInput input, MaterialTextureSet textures, uint samplerId = 0)
{
    Material material = SampleMaterial(textures, samplerId, input.uv);
    float3 shadingNormal = SampleNormal(textures, samplerId, input.uv, input.geometricNormal, input.worldPos, input.tangent);
    PBRSurface surface = BuildPBRSurface(input, material, shadingNormal);
    return EvaluatePBR(surface);
}
