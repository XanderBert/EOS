// Microfacet building blocks: GGX distribution, Smith masking-shadowing and Fresnel.
// Angles are given as cosines against the shading normal.
module "eos/brdf";

import eos.core;
import eos.math;

// Below this perceptual roughness GGX turns into a spike: 0/0 at N·H = 1, fireflies close to it. [Filament]
public static const float kMinPerceptualRoughness = 0.045;

// Reflectance at normal incidence of a dielectric with IOR 1.5, as glTF 2.0 uses for non-metals.
public static const float kDielectricF0 = 0.04;

public float PerceptualRoughnessToAlpha(float perceptualRoughness)
{
    const float roughness = clamp(perceptualRoughness, kMinPerceptualRoughness, 1.0);
    return roughness * roughness;
}

// GGX (Trowbridge-Reitz) normal distribution. cosThetaH = N·H.
public float D_GGX(float alpha, float cosThetaH)
{
    if (cosThetaH <= 0.0) return 0.0;

    const float alpha2 = alpha * alpha;
    const float d = cosThetaH * cosThetaH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (kPi * d * d);
}

// Smith Λ for GGX. [Heitz 2014, "Understanding the Masking-Shadowing Function"]
public float Lambda_GGX(float alpha, float cosTheta)
{
    const float cos2 = cosTheta * cosTheta;
    const float tan2 = max(1.0 - cos2, 0.0) / max(cos2, 1e-12);
    return 0.5 * (sqrt(1.0 + alpha * alpha * tan2) - 1.0);
}

// Smith masking for one direction.
public float G1_GGX(float alpha, float cosTheta)
{
    if (cosTheta <= 0.0) return 0.0;
    return 1.0 / (1.0 + Lambda_GGX(alpha, cosTheta));
}

// Height-correlated Smith masking-shadowing for a pair of directions. [Heitz 2014]
public float G2_GGX(float alpha, float cosThetaI, float cosThetaO)
{
    if (cosThetaI <= 0.0 || cosThetaO <= 0.0) return 0.0;
    return 1.0 / (1.0 + Lambda_GGX(alpha, cosThetaI) + Lambda_GGX(alpha, cosThetaO));
}

// Schlick's Fresnel approximation with f90 = 1.
public float3 F_Schlick(float3 f0, float cosTheta)
{
    return f0 + (1.0 - f0) * Pow5(1.0 - saturate(cosTheta));
}

public float F_Schlick(float f0, float cosTheta)
{
    return f0 + (1.0 - f0) * Pow5(1.0 - saturate(cosTheta));
}

// Analytic fit of the split-sum environment BRDF integral over the hemisphere, for directional albedo estimates.
// Returns (scale, bias) so that the integral of the specular lobe is f0 * scale + bias.
// [Karis 2014, "Physically Based Shading on Mobile"]
public float2 EnvironmentBRDFApprox(float cosThetaO, float perceptualRoughness)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = perceptualRoughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * saturate(cosThetaO))) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}
