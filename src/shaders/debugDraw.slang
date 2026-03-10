module debugDraw;

static const float kDebugMinSmoothWidth = 1e-5f;
static const float kDebugMinSegmentLengthSq = 1e-8f;

public float SphereSDF(float3 positionWS, float3 centerWS, float radius)
{
    return length(positionWS - centerWS) - radius;
}

public float SphereFillMask(float3 positionWS, float3 centerWS, float radius, float feather = 0.01f)
{
    float signedDistance = SphereSDF(positionWS, centerWS, radius);
    float smoothWidth = max(feather, kDebugMinSmoothWidth);
    return 1.0f - smoothstep(0.0f, smoothWidth, signedDistance);
}

public float SphereWireMask(float3 positionWS, float3 centerWS, float radius, float thickness = 0.02f, float feather = 0.01f)
{
    float signedDistance = abs(SphereSDF(positionWS, centerWS, radius));
    float halfThickness = max(thickness * 0.5f, kDebugMinSmoothWidth);
    float smoothWidth = max(feather, kDebugMinSmoothWidth);
    return 1.0f - smoothstep(halfThickness, halfThickness + smoothWidth, signedDistance);
}

public float BoxSDF(float3 positionWS, float3 centerWS, float3 halfExtentWS)
{
    float3 delta = abs(positionWS - centerWS) - halfExtentWS;
    return length(max(delta, 0.0f)) + min(max(delta.x, max(delta.y, delta.z)), 0.0f);
}

public float BoxFillMask(float3 positionWS, float3 centerWS, float3 halfExtentWS, float feather = 0.01f)
{
    float signedDistance = BoxSDF(positionWS, centerWS, halfExtentWS);
    float smoothWidth = max(feather, kDebugMinSmoothWidth);
    return 1.0f - smoothstep(0.0f, smoothWidth, signedDistance);
}

public float BoxWireMask(float3 positionWS, float3 centerWS, float3 halfExtentWS, float thickness = 0.02f, float feather = 0.01f)
{
    float signedDistance = abs(BoxSDF(positionWS, centerWS, halfExtentWS));
    float halfThickness = max(thickness * 0.5f, kDebugMinSmoothWidth);
    float smoothWidth = max(feather, kDebugMinSmoothWidth);
    return 1.0f - smoothstep(halfThickness, halfThickness + smoothWidth, signedDistance);
}

public float SegmentDistance(float3 positionWS, float3 startWS, float3 endWS)
{
    float3 segment = endWS - startWS;
    float segmentLengthSq = dot(segment, segment);

    if (segmentLengthSq <= kDebugMinSegmentLengthSq)
    {
        return length(positionWS - startWS);
    }

    float t = saturate(dot(positionWS - startWS, segment) / segmentLengthSq);
    float3 closestPoint = startWS + t * segment;
    return length(positionWS - closestPoint);
}

public float SegmentMask(float3 positionWS, float3 startWS, float3 endWS, float radius, float feather = 0.01f)
{
    float signedDistance = SegmentDistance(positionWS, startWS, endWS) - radius;
    float smoothWidth = max(feather, kDebugMinSmoothWidth);
    return 1.0f - smoothstep(0.0f, smoothWidth, signedDistance);
}

public float3 BlendDebugColor(float3 baseColor, float3 debugColor, float mask, float opacity = 1.0f)
{
    return lerp(baseColor, debugColor, saturate(mask * opacity));
}

public float4 BlendDebugColor(float4 baseColor, float4 debugColor, float mask, float opacity = 1.0f)
{
    return lerp(baseColor, debugColor, saturate(mask * opacity));
}

public float3 DrawSphere(float3 baseColor, float3 positionWS, float3 centerWS, float radius, float3 sphereColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SphereFillMask(positionWS, centerWS, radius, feather);
    return BlendDebugColor(baseColor, sphereColor, mask, opacity);
}

public float3 DrawSphereWire(float3 baseColor, float3 positionWS, float3 centerWS, float radius, float thickness, float3 sphereColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SphereWireMask(positionWS, centerWS, radius, thickness, feather);
    return BlendDebugColor(baseColor, sphereColor, mask, opacity);
}

public float3 DrawBox(float3 baseColor, float3 positionWS, float3 centerWS, float3 halfExtentWS, float3 boxColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = BoxFillMask(positionWS, centerWS, halfExtentWS, feather);
    return BlendDebugColor(baseColor, boxColor, mask, opacity);
}

public float3 DrawBoxWire(float3 baseColor, float3 positionWS, float3 centerWS, float3 halfExtentWS, float thickness, float3 boxColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = BoxWireMask(positionWS, centerWS, halfExtentWS, thickness, feather);
    return BlendDebugColor(baseColor, boxColor, mask, opacity);
}

public float3 DrawSegment(float3 baseColor, float3 positionWS, float3 startWS, float3 endWS, float radius, float3 segmentColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SegmentMask(positionWS, startWS, endWS, radius, feather);
    return BlendDebugColor(baseColor, segmentColor, mask, opacity);
}

public float4 DrawSphere(float4 baseColor, float3 positionWS, float3 centerWS, float radius, float4 sphereColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SphereFillMask(positionWS, centerWS, radius, feather);
    return BlendDebugColor(baseColor, sphereColor, mask, opacity);
}

public float4 DrawSphereWire(float4 baseColor, float3 positionWS, float3 centerWS, float radius, float thickness, float4 sphereColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SphereWireMask(positionWS, centerWS, radius, thickness, feather);
    return BlendDebugColor(baseColor, sphereColor, mask, opacity);
}

public float4 DrawBox(float4 baseColor, float3 positionWS, float3 centerWS, float3 halfExtentWS, float4 boxColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = BoxFillMask(positionWS, centerWS, halfExtentWS, feather);
    return BlendDebugColor(baseColor, boxColor, mask, opacity);
}

public float4 DrawBoxWire(float4 baseColor, float3 positionWS, float3 centerWS, float3 halfExtentWS, float thickness, float4 boxColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = BoxWireMask(positionWS, centerWS, halfExtentWS, thickness, feather);
    return BlendDebugColor(baseColor, boxColor, mask, opacity);
}

public float4 DrawSegment(float4 baseColor, float3 positionWS, float3 startWS, float3 endWS, float radius, float4 segmentColor, float opacity = 1.0f, float feather = 0.01f)
{
    float mask = SegmentMask(positionWS, startWS, endWS, radius, feather);
    return BlendDebugColor(baseColor, segmentColor, mask, opacity);
}