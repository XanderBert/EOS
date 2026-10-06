// BSDF interface and the glTF 2.0 metallic-roughness BSDF.
//
// A BSDF works in a local shading frame where z is the shading normal. wo points from the surface towards the viewer,
// wi towards the light; both are unit vectors. The same BSDF serves the rasterizer (Eval per light) and a path tracer
// (Sample + EvalPdf for importance sampling and multiple importance sampling).
module "eos/bsdf";

import eos.core;
import eos.math;
import eos.color;
import eos.brdf;
import eos.sampling;

// Orthonormal frame a BSDF is expressed in: tangent T, bitangent B and shading normal N as the z axis.
public struct ShadingFrame
{
    public float3 T;
    public float3 B;
    public float3 N;

    // A frame around a normal, with an arbitrary tangent.
    public static ShadingFrame FromNormal(float3 normal)
    {
        ShadingFrame frame;
        frame.N = normal;
        BuildOrthonormalBasis(normal, frame.T, frame.B);
        return frame;
    }

    // A frame around a normal with a glTF-style tangent: xyz is the tangent, w (+1 or -1) the bitangent sign, which
    // flips for mirrored UVs. The tangent is re-orthogonalized against the normal. A missing tangent (w = 0) or one
    // parallel to the normal falls back to an arbitrary tangent.
    public static ShadingFrame FromNormalAndTangent(float3 normal, float4 tangent)
    {
        const float3 orthogonalTangent = tangent.xyz - normal * dot(normal, tangent.xyz);
        const float lengthSquared = dot(orthogonalTangent, orthogonalTangent);
        if (tangent.w == 0.0 || lengthSquared < 1e-12) return FromNormal(normal);

        ShadingFrame frame;
        frame.N = normal;
        frame.T = orthogonalTangent * rsqrt(lengthSquared);
        frame.B = cross(normal, frame.T) * (tangent.w < 0.0 ? -1.0 : 1.0);
        return frame;
    }

    public float3 ToLocal(float3 direction)
    {
        return float3(dot(direction, T), dot(direction, B), dot(direction, N));
    }

    public float3 FromLocal(float3 direction)
    {
        return T * direction.x + B * direction.y + N * direction.z;
    }
}

// Which kind of scattering a sample came from. A path tracer can treat them differently, e.g. only follow specular
// paths for caustics. Transmission and delta lobes are listed so BSDFs that have them fit the same interface.
public static const uint kBSDFLobeNone                  = 0;
public static const uint kBSDFLobeDiffuseReflection     = 1u << 0;
public static const uint kBSDFLobeSpecularReflection    = 1u << 1;
public static const uint kBSDFLobeDiffuseTransmission   = 1u << 2;
public static const uint kBSDFLobeSpecularTransmission  = 1u << 3;
public static const uint kBSDFLobeDelta                 = 1u << 4;   // perfect mirror or glass: no finite pdf

public struct BSDFSample
{
    public float3 wi;        // sampled direction towards the light, local frame
    public float3 weight;    // Eval(wo, wi) / pdf: what the path throughput gets multiplied with
    public float pdf;        // solid-angle pdf of wi
    public uint lobe;        // kBSDFLobe* of the lobe that produced wi
}

public interface IBSDF
{
    // f(wo, wi) · cosθi: the reflected radiance towards wo per unit irradiance from wi. Zero below the surface.
    float3 Eval(float3 wo, float3 wi);

    // Importance-samples wi for wo. Returns false when no direction was produced; the path should end then.
    bool Sample(float3 wo, inout RNG rng, out BSDFSample result);

    // The solid-angle pdf with which Sample picks wi, for multiple importance sampling.
    float EvalPdf(float3 wo, float3 wi);

    // Approximate hemispherical reflectance seen from wo: lighting from a constant environment, denoiser albedo
    // guides and Russian roulette.
    float3 Albedo(float3 wo);
}

// glTF 2.0 core metallic-roughness BSDF, following the specification's reference model (Appendix B):
//     dielectric = fresnel_mix(ior 1.5, base: Lambert(baseColor), layer: GGX)
//     metal      = conductor_fresnel(f0: baseColor, GGX)
//     bsdf       = mix(dielectric, metal, metallic)
// except that the diffuse base is weighted by 1 - E(wo), the directional albedo of the dielectric specular layer, rather
// than by 1 - F(wo·h) per microfacet: the latter reflects more light than arrives at grazing angles (up to 1.5x for a
// white base). Like the reference model, the GGX lobes are single scattering and lose energy at high roughness.
// Extensions add lobes on top of these two: clearcoat as a dielectric GGX layer that attenuates everything below by
// its Fresnel, sheen as an albedo-scaled lobe over the base, transmission as a GGX refraction lobe that replaces part
// of the diffuse one. Each new lobe takes part in lobe selection by its estimated weight, like the two below.
public struct StandardBSDF : IBSDF
{
    public float3 baseColor;            // linear
    public float metallic;
    public float perceptualRoughness;   // clamped to kMinPerceptualRoughness
    public float alpha;                 // GGX alpha

    public __init(float3 baseColor, float metallic, float perceptualRoughness)
    {
        this.baseColor = baseColor;
        this.metallic = saturate(metallic);
        this.perceptualRoughness = clamp(perceptualRoughness, kMinPerceptualRoughness, 1.0);
        this.alpha = PerceptualRoughnessToAlpha(this.perceptualRoughness);
    }

    public float3 SpecularF0()
    {
        return lerp(float3(kDielectricF0), baseColor, metallic);
    }

    public float3 Eval(float3 wo, float3 wi)
    {
        if (wo.z <= 0.0 || wi.z <= 0.0) return float3(0.0);

        const float3 h = normalize(wo + wi);
        const float cosThetaD = saturate(dot(wo, h));

        // Mixing the dielectric and conductor Fresnel by metallic gives Schlick with a mixed f0.
        const float3 F = F_Schlick(SpecularF0(), cosThetaD);
        const float3 specular = F * (D_GGX(alpha, h.z) * G2_GGX(alpha, wi.z, wo.z) / (4.0 * wo.z));

        // Only the dielectric part has a diffuse base, lit by what its specular layer did not reflect.
        const float3 diffuse = (1.0 - metallic) * (1.0 - DielectricSpecularAlbedo(wo.z)) * baseColor * (kInvPi * wi.z);

        return diffuse + specular;
    }

    public bool Sample(float3 wo, inout RNG rng, out BSDFSample result)
    {
        result.wi = float3(0.0);
        result.weight = float3(0.0);
        result.pdf = 0.0;
        result.lobe = kBSDFLobeNone;
        if (wo.z <= 0.0) return false;

        const float2 u = rng.NextFloat2();
        if (rng.NextFloat() < DiffuseSelectionProbability(wo))
        {
            result.wi = SampleCosineHemisphere(u);
            result.lobe = kBSDFLobeDiffuseReflection;
        }
        else
        {
            const float3 h = SampleGGXVisibleNormal(wo, float2(alpha), u);
            result.wi = reflect(-wo, h);
            result.lobe = kBSDFLobeSpecularReflection;
        }

        if (result.wi.z <= 0.0) return false;

        // The pdf of the whole mixture, so every sample is weighted the same no matter which lobe produced it.
        result.pdf = EvalPdf(wo, result.wi);
        if (result.pdf <= 0.0) return false;

        result.weight = Eval(wo, result.wi) / result.pdf;
        return true;
    }

    public float EvalPdf(float3 wo, float3 wi)
    {
        if (wo.z <= 0.0 || wi.z <= 0.0) return 0.0;

        // Visible normal pdf of h converted to the reflected direction: G1(wo) D(h) (wo·h) / wo.z / (4 wo·h).
        const float3 h = normalize(wo + wi);
        const float specularPdf = G1_GGX(alpha, wo.z) * D_GGX(alpha, h.z) / (4.0 * wo.z);

        const float diffuseProbability = DiffuseSelectionProbability(wo);
        return diffuseProbability * CosineHemispherePdf(wi.z) + (1.0 - diffuseProbability) * specularPdf;
    }

    public float3 Albedo(float3 wo)
    {
        const float cosThetaO = saturate(wo.z);
        const float2 environment = EnvironmentBRDFApprox(cosThetaO, perceptualRoughness);
        const float3 specular = SpecularF0() * environment.x + environment.y;
        const float3 diffuse = (1.0 - metallic) * (1.0 - DielectricSpecularAlbedo(cosThetaO)) * baseColor;
        return diffuse + specular;
    }

    // Fraction of the light arriving from wo that the dielectric specular layer reflects, over the whole hemisphere.
    float DielectricSpecularAlbedo(float cosThetaO)
    {
        const float2 environment = EnvironmentBRDFApprox(saturate(cosThetaO), perceptualRoughness);
        return saturate(kDielectricF0 * environment.x + environment.y);
    }

    // Picks the diffuse lobe in proportion to how much it is expected to contribute compared to the specular one.
    float DiffuseSelectionProbability(float3 wo)
    {
        const float diffuseWeight = (1.0 - metallic) * (1.0 - DielectricSpecularAlbedo(wo.z)) * Luminance(baseColor);
        const float specularWeight = Luminance(F_Schlick(SpecularF0(), wo.z));
        const float totalWeight = diffuseWeight + specularWeight;
        return totalWeight > 0.0 ? diffuseWeight / totalWeight : 0.0;
    }
}
