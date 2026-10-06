// General math helpers. Matrices follow the EOS convention: row-major storage, row vectors (mul(v, M)).
module "eos/math";

import eos.core;

public float Square(float x)
{
    return x * x;
}

public float Pow5(float x)
{
    const float x2 = x * x;
    return x2 * x2 * x;
}

public float MaxComponent(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

// [Drobot2014] https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
// About 3% error; only for values that do not need to be accurate.
public float sqrtFast(float x)
{
    return asfloat(0x1fbd1df5 + (asuint(x) >> 1));
}

// https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
public float acosFast(float inX)
{
    const float x = abs(inX);
    float res = -0.156583 * x + kHalfPi;
    res *= sqrtFast(1.0 - x);
    return (inX >= 0.0) ? res : kPi - res;
}

// View-space distance of a [0, 1] depth value from a standard (not reversed) perspective projection.
public float LinearDepth(float depth, float nearPlane, float farPlane)
{
    return (nearPlane * farPlane) / (farPlane - depth * (farPlane - nearPlane));
}

public float3 UnprojectNDC(float3 ndc, float4x4 inverseViewProjection)
{
    const float4 world = mul(float4(ndc, 1.0), inverseViewProjection);
    return world.xyz / world.w;
}

public float4x4 BuildLookAtRH(float3 eye, float3 target, float3 up)
{
    const float3 f = normalize(eye - target);
    const float3 s = normalize(cross(f, up));
    const float3 u = cross(s, f);

    return float4x4(
        s.x,  u.x, -f.x, 0.0,
        s.y,  u.y, -f.y, 0.0,
        s.z,  u.z, -f.z, 0.0,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0
    );
}

// Orthographic projection to Vulkan's [0, 1] depth range.
public float4x4 BuildOrthoZO(float left, float right, float bottom, float top, float zNear, float zFar)
{
    return float4x4(
        2.0 / (right - left), 0.0, 0.0, 0.0,
        0.0, -2.0 / (top - bottom), 0.0, 0.0,
        0.0, 0.0, 1.0 / (zFar - zNear), 0.0,
        -(right + left) / (right - left),
        -(top + bottom) / (top - bottom),
        -zNear / (zFar - zNear),
        1.0
    );
}

public bool IsFiniteFloat3(float3 v)
{
    return all(isfinite(v));
}

public float3 SafeNormalize(float3 v, float3 fallback)
{
    const float lengthSquared = dot(v, v);
    if (lengthSquared <= 1e-12 || !IsFiniteFloat3(v)) return fallback;
    return v * rsqrt(lengthSquared);
}

// Unit vector stored in an unsigned normalized format: [-1, 1] -> [0, 1].
public float3 PackNormalUnorm(float3 normal)
{
    return normal * 0.5 + 0.5;
}

public float3 UnpackNormalUnorm(float3 packedNormal)
{
    return normalize(packedNormal * 2.0 - 1.0);
}

// Builds tangent and bitangent for a unit normal without branches or singularities.
// [Duff et al. 2017, "Building an Orthonormal Basis, Revisited"]
public void BuildOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    const float s = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (s + n.z);
    const float b = n.x * n.y * a;
    tangent = float3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    bitangent = float3(b, s + n.y * n.y * a, -n.y);
}
