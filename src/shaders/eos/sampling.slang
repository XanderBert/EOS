// Random numbers and the sampling routines BSDFs and path tracers need.
// Directions are in a local frame where z is the surface normal.
module "eos/sampling";

import eos.core;

// [Jarzynski & Olano 2020, "Hash Functions for GPU Rendering"], PCG RXS-M-XS.
public uint PcgHash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Small, fast random number generator (32-bit PCG). One per pixel/path, seeded from the pixel and frame.
public struct RNG
{
    uint state;

    public __init(uint seed)
    {
        state = seed;
    }

    // Decorrelates neighbouring pixels and consecutive frames.
    public static RNG Create(uint2 pixel, uint frameIndex)
    {
        return RNG(PcgHash(pixel.x + PcgHash(pixel.y + PcgHash(frameIndex))));
    }

    [mutating]
    public uint NextUInt()
    {
        state = state * 747796405u + 2891336453u;
        const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    // Uniform in [0, 1).
    [mutating]
    public float NextFloat()
    {
        return float(NextUInt() >> 8) * (1.0 / 16777216.0);
    }

    [mutating]
    public float2 NextFloat2()
    {
        const float x = NextFloat();
        return float2(x, NextFloat());
    }
}

// Maps the unit square to the unit disk with low distortion. [Shirley & Chiu 1997]
public float2 SampleConcentricDisk(float2 u)
{
    const float2 offset = 2.0 * u - 1.0;
    if (offset.x == 0.0 && offset.y == 0.0) return float2(0.0);

    float radius;
    float theta;
    if (abs(offset.x) > abs(offset.y))
    {
        radius = offset.x;
        theta = (kPi / 4.0) * (offset.y / offset.x);
    }
    else
    {
        radius = offset.y;
        theta = kHalfPi - (kPi / 4.0) * (offset.x / offset.y);
    }

    return radius * float2(cos(theta), sin(theta));
}

// Direction with pdf cosθ / π.
public float3 SampleCosineHemisphere(float2 u)
{
    const float2 disk = SampleConcentricDisk(u);
    return float3(disk, sqrt(max(0.0, 1.0 - dot(disk, disk))));
}

public float CosineHemispherePdf(float cosTheta)
{
    return max(cosTheta, 0.0) * kInvPi;
}

public float3 SampleUniformSphere(float2 u)
{
    const float z = 1.0 - 2.0 * u.x;
    const float r = sqrt(max(0.0, 1.0 - z * z));
    const float phi = kTwoPi * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

public float UniformSpherePdf()
{
    return 1.0 / (4.0 * kPi);
}

// Samples a microfacet normal from the GGX distribution of normals visible from wo (wo.z > 0).
// [Dupuy & Benyoub 2023, "Sampling Visible GGX Normals with Spherical Caps"]
public float3 SampleGGXVisibleNormal(float3 wo, float2 alpha, float2 u)
{
    // Stretch to the hemisphere configuration, sample a spherical cap there, then unstretch.
    const float3 woStandard = normalize(float3(wo.xy * alpha, wo.z));
    const float phi = kTwoPi * u.x;
    const float z = (1.0 - u.y) * (1.0 + woStandard.z) - woStandard.z;
    const float sinTheta = sqrt(saturate(1.0 - z * z));
    const float3 capDirection = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    const float3 normalStandard = capDirection + woStandard;
    return normalize(float3(normalStandard.xy * alpha, max(normalStandard.z, 0.0)));
}

// Multiple importance sampling weights for one sample from each of two strategies. [Veach 1997]
public float BalanceHeuristic(float pdfChosen, float pdfOther)
{
    return pdfChosen / max(pdfChosen + pdfOther, 1e-20);
}

public float PowerHeuristic(float pdfChosen, float pdfOther)
{
    const float chosen = pdfChosen * pdfChosen;
    return chosen / max(chosen + pdfOther * pdfOther, 1e-20);
}
