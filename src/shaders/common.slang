module common;

#define PI      3.14159265358979323846
#define HALF_PI			(0.5f * PI)

[[vk::binding(0, 0)]] Texture2D kTextures2D[];
[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(3, 0)]] Texture2DArray kTextures2DArray[];

public float4 TextureBindless2D(uint textureId, uint samplerId, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uv);
}

public float4 TextureBindless2DLod(uint textureId, uint samplerId, float2 uv, float lod)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].SampleLevel(kSamplers[NonUniformResourceIndex(samplerId)], uv, lod);
}

public float4 TextureBindless2DLoad(uint textureId, int2 pixel)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].Load(int3(pixel, 0));
}

public float4 TextureBindless2DArray(uint textureId, uint samplerId, float3 uvw)
{
    return kTextures2DArray[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uvw);
}

public T LoadBuffer<T>(uint64_t address)
{
    return *((Ptr<T>)address);
}

public T LoadBufferArray<T>(uint64_t address, uint64_t  index)
{
    return *((Ptr<T>)address + index);
}

public struct Material
{
    public float3 albedo;
    public float metallic;
    public float roughness;
}

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

float3 ReconstructNormal(float2 normalSample)
{
    float2 xy = 2.0 * normalSample - float2(1.0); // unpack from [0,1] to [-1,1]
    float z = sqrtFast(clamp(1.0 - dot(xy, xy), 0.0, 1.0));
    return normalize(float3(xy, z));
}

public float3 GetShadingNormal(float3 n, float3 v, float2 normalSample, float2 uv, float3 tangent)
{
    float3 tangentNormal = ReconstructNormal(normalSample);
    float3 N = normalize(n);
    float3 T = normalize(tangent);
    float3 B = normalize(cross(N, T));

    float3x3 TBN = transpose(float3x3(T, B, N));

    return normalize(mul(TBN, tangentNormal));
}


public float LinearDepth01(float depth, float near, float far)
{
    //return (depth - near) / (far - near);
    float zLinear = (near * far) / (far - depth * (far - near));
    //return zLinear;
    return (zLinear - near) / (far - near); // normalized to [0,1]
}