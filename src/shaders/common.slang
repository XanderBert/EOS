module common;

#define PI      3.14159265358979323846
#define HALF_PI			(0.5f * PI)

[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(0, 0)]] Texture2D kTextures2D[];

public float4 TextureBindless2D(uint textureId, uint samplerId, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uv);
}

public T LoadBuffer<T>(uint64_t address)
{
    return *((Ptr<T>)address);
}

public struct Material
{
    public float3 albedo;
    public float metallic;
    public float roughness;
};


//[Drobot2014] https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
public float sqrtFast(float x)
{
    return asfloat(0x1fbd1df5 + (asuint(x) >> 1));
}

//https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
public float acosFast(float inX)
{
    float x = abs(inX);
    float res = -0.156583 * x + float(HALF_PI);
    res *= sqrtFast(1.0 - x);
    return (inX >= 0.0) ? res : float(PI) - res;
}

// http://www.thetenthplanet.de/archives/1180
// modified to fix handedness of the resulting cotangent frame - Source: LVK
float3x3 CotangentFrame(float3 N, float3 p, float2 uv)
{
    // get edge vectors of the pixel triangle
    float3 dp1  = ddx(p);
    float3 dp2  = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    if (length(duv1) <= 1e-2)
        duv1 = float2(1.0, 0.0);

    if (length(duv2) <= 1e-2)
        duv2 = float2(0.0, 1.0);

    // solve the linear system
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));

    // calculate handedness of the resulting cotangent frame
    float w = dot(cross(N, T), B) < 0.0 ? -1.0 : 1.0;

    // adjust tangent if needed
    T = T * w;

    return float3x3(T * invmax, B * invmax, N);
}


public float3 ReconstructNormal(float2 normalSample)
{
    float2 xy = 2.0 * normalSample - float2(1.0); // unpack from [0,1] to [-1,1]
    float z = sqrtFast(clamp(1.0 - dot(xy, xy), 0.0, 1.0));
    return normalize(float3(xy, z));
}

public float3 GetShadingNormal(float3 n, float3 v, float2 normalSample, float2 uv)
{
    float3 map = ReconstructNormal(normalSample);
    float3x3 TBN = CotangentFrame(n, v, uv);
    return normalize(mul(TBN, map));
}