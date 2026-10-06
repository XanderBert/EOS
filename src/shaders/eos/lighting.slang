// Direct lighting for rasterized shading, on top of any IBSDF.
// Lights carry radiometric quantities: the color times the intensity, in linear RGB.
module "eos/lighting";

import eos.bsdf;

public struct DirectionalLight
{
    public float3 direction;    // direction the light travels in, unit length
    public float3 radiance;
}

public struct PointLight
{
    public float3 position;
    public float3 intensity;    // falls off with the squared distance
}

// The view direction in the BSDF's frame. Normal mapping can tilt the shading normal away from the viewer; such
// directions are moved just above the horizon so the surface does not turn black.
float3 ViewDirectionToLocal(ShadingFrame frame, float3 viewDirection)
{
    float3 wo = frame.ToLocal(viewDirection);
    wo.z = max(wo.z, 1e-4);
    return normalize(wo);
}

// Light from a directional light reflected towards the viewer. Shadowing is up to the caller.
public float3 EvaluateDirectionalLight<B : IBSDF>(B bsdf, ShadingFrame frame, float3 viewDirection, DirectionalLight light)
{
    return bsdf.Eval(ViewDirectionToLocal(frame, viewDirection), frame.ToLocal(-light.direction)) * light.radiance;
}

public float3 EvaluatePointLight<B : IBSDF>(B bsdf, ShadingFrame frame, float3 position, float3 viewDirection, PointLight light)
{
    const float3 toLight = light.position - position;
    const float distanceSquared = max(dot(toLight, toLight), 1e-8);
    const float3 wi = frame.ToLocal(toLight * rsqrt(distanceSquared));
    return bsdf.Eval(ViewDirectionToLocal(frame, viewDirection), wi) * light.intensity / distanceSquared;
}

// A uniform environment of the given radiance reflected towards the viewer: a stand-in for image-based lighting.
public float3 EvaluateConstantAmbient<B : IBSDF>(B bsdf, ShadingFrame frame, float3 viewDirection, float3 radiance, float occlusion)
{
    return bsdf.Albedo(ViewDirectionToLocal(frame, viewDirection)) * radiance * occlusion;
}
