import bindings;
import math;
import shadowCommon;

static const uint CASCADES = 4;
static const float kCascadeOverlapNDC = 0.03;

struct DepthReductionData
{
    float minDepth;
    float maxDepth;
    float pad0;
    float pad1;
};

struct CascadeSetupPC
{
    uint64_t perFramePtr;
    uint64_t depthRangePtr;
    float4x4 invViewProjection;
    float4 lightForwardNear;
    float4 cameraPlanes;
};

[[vk::push_constant]] CascadeSetupPC pc;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x != 0 || dispatchThreadID.y != 0 || dispatchThreadID.z != 0)
    {
        return;
    }

    float nearClip = pc.cameraPlanes.x;
    float farClip = pc.cameraPlanes.y;
    float clipRange = farClip - nearClip;
    bool useDepthReduction = pc.cameraPlanes.z > 0.5;
    float shadowResolution = max(pc.cameraPlanes.w, 1.0);

    float sceneMin = nearClip;
    float sceneMax = nearClip + clipRange;

    if (useDepthReduction)
    {
        DepthReductionData reduction = *((Ptr<DepthReductionData>)pc.depthRangePtr);
        if (reduction.maxDepth > reduction.minDepth)
        {
            float reducedMin = LinearDepth(reduction.minDepth, nearClip, farClip);
            float reducedMax = LinearDepth(reduction.maxDepth, nearClip, farClip);
            sceneMin = clamp(reducedMin, nearClip, farClip);
            sceneMax = clamp(reducedMax, nearClip, farClip);
            sceneMax = max(sceneMax, sceneMin + 1e-4);
        }
    }

    float range = sceneMax - sceneMin;
    float ratio = sceneMax / sceneMin;
    float cascadeSplits[CASCADES];

    [unroll]
    for (uint i = 0; i < CASCADES; ++i)
    {
        float p = (i + 1) / float(CASCADES);
        float logSplit = sceneMin * pow(ratio, p);
        float uniformSplit = sceneMin + range * p;
        float split = lerp(uniformSplit, logSplit, 0.25);
        cascadeSplits[i] = (split - nearClip) / clipRange;
    }

    float lastSplitDist = 0.0;
    float3 lightForward = normalize(pc.lightForwardNear.xyz);

    PerFrameData frameData = *((Ptr<PerFrameData>)pc.perFramePtr);

    [unroll]
    for (uint i = 0; i < CASCADES; ++i)
    {
        float splitDist = cascadeSplits[i];
        float cascadeNearDist = max(lastSplitDist - kCascadeOverlapNDC, 0.0);
        float cascadeFarDist = min(splitDist + kCascadeOverlapNDC, 1.0);

        float3 frustumCorners[8];
        frustumCorners[0] = UnprojectNDC(float3(-1.0,  1.0, 0.0), pc.invViewProjection);
        frustumCorners[1] = UnprojectNDC(float3( 1.0,  1.0, 0.0), pc.invViewProjection);
        frustumCorners[2] = UnprojectNDC(float3( 1.0, -1.0, 0.0), pc.invViewProjection);
        frustumCorners[3] = UnprojectNDC(float3(-1.0, -1.0, 0.0), pc.invViewProjection);
        frustumCorners[4] = UnprojectNDC(float3(-1.0,  1.0, 1.0), pc.invViewProjection);
        frustumCorners[5] = UnprojectNDC(float3( 1.0,  1.0, 1.0), pc.invViewProjection);
        frustumCorners[6] = UnprojectNDC(float3( 1.0, -1.0, 1.0), pc.invViewProjection);
        frustumCorners[7] = UnprojectNDC(float3(-1.0, -1.0, 1.0), pc.invViewProjection);

        [unroll]
        for (uint j = 0; j < 4; ++j)
        {
            float3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + dist * cascadeFarDist;
            frustumCorners[j] = frustumCorners[j] + dist * cascadeNearDist;
        }

        float3 frustumCenter = 0.0;
        [unroll]
        for (uint j = 0; j < 8; ++j)
        {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0;

        float radius = 0.0;
        [unroll]
        for (uint j = 0; j < 8; ++j)
        {
            radius = max(radius, length(frustumCorners[j] - frustumCenter));
        }
        radius = ceil(radius * 16.0) / 16.0;

        
        float3 maxExtents = float3(radius, radius, radius);
        float3 minExtents = -maxExtents;

        float3 lightUp = float3(0.0, 1.0, 0.0);
        float3 lightPos = frustumCenter - lightForward * -minExtents.z;

        float4x4 lightView = BuildLookAtRH(lightPos, frustumCenter, lightUp);
        float4x4 lightOrtho = BuildOrthoZO(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0, maxExtents.z - minExtents.z);

        // Texel-grid stabilization to reduce shimmering while moving the camera.
        float4x4 shadowMatrix = mul(lightView, lightOrtho);
        float4 shadowOrigin = mul(float4(0.0, 0.0, 0.0, 1.0), shadowMatrix);
        shadowOrigin *= (shadowResolution * 0.5);

        float4 roundedOrigin = round(shadowOrigin);
        float4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0 / shadowResolution);
        roundOffset.z = 0.0;
        roundOffset.w = 0.0;

        float4 orthoTranslation = lightOrtho[3];
        orthoTranslation.xyz += roundOffset.xyz;
        lightOrtho[3] = orthoTranslation;

        frameData.cascadeViewProj[i] = mul(lightView, lightOrtho);
        lastSplitDist = splitDist;
    }

    frameData.cascadeSplits = float4(
        nearClip + cascadeSplits[0] * clipRange,
        nearClip + cascadeSplits[1] * clipRange,
        nearClip + cascadeSplits[2] * clipRange,
        nearClip + cascadeSplits[3] * clipRange
    );

    *((Ptr<PerFrameData>)pc.perFramePtr) = frameData;
}