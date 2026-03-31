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
