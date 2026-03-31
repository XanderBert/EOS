module math;

#define PI 3.14159265358979323846
#define HALF_PI (0.5f * PI)

// [Drobot2014] https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf
public float sqrtFast(float x)
{
    return asfloat(0x1fbd1df5 + (asuint(x) >> 1));
}

// https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
public float acosFast(float inX)
{
    float x = abs(inX);
    float res = -0.156583 * x + float(HALF_PI);
    res *= sqrtFast(1.0 - x);
    return (inX >= 0.0) ? res : float(PI) - res;
}

public float LinearDepth(float depth, float nearPlane, float farPlane)
{
    return (nearPlane * farPlane) / (farPlane - depth * (farPlane - nearPlane));
}

public float3 UnprojectNDC(float3 ndc, float4x4 inverseViewProjection)
{
    float4 world = mul(float4(ndc, 1.0), inverseViewProjection);
    return world.xyz / world.w;
}

public float4x4 BuildLookAtRH(float3 eye, float3 target, float3 up)
{
    float3 f = normalize(eye - target);
    float3 s = normalize(cross(f, up));
    float3 u = cross(s, f);

    float4x4 m = float4x4(
        s.x,  u.x, -f.x, 0.0,
        s.y,  u.y, -f.y, 0.0,
        s.z,  u.z, -f.z, 0.0,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0
    );

    return m;
}

public float4x4 BuildOrthoZO(float left, float right, float bottom, float top, float zNear, float zFar)
{
    float4x4 m = float4x4(
        2.0 / (right - left), 0.0, 0.0, 0.0,
        0.0, -2.0 / (top - bottom), 0.0, 0.0,
        0.0, 0.0, 1.0 / (zFar - zNear), 0.0,
        -(right + left) / (right - left),
        -(top + bottom) / (top - bottom),
        -zNear / (zFar - zNear),
        1.0
    );

    return m;
}

public bool IsFiniteFloat3(float3 v)
{
    return all(!isnan(v)) && all(!isinf(v));
}

public float3 SafeNormalize(float3 v, float3 fallback)
{
    float lenSq = dot(v, v);
    if (lenSq <= 1e-12 || !IsFiniteFloat3(v))
    {
        return fallback;
    }

    return v * rsqrt(lenSq);
}