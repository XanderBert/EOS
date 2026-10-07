// Fits the shadow cascades to the depth range the depth reduction found, on the GPU, and writes their matrices and
// splits for the shadow and shade passes. One thread does it all. The cpuCascades node (main.cpp) does the same on the
// CPU, over the camera's whole depth range.
import eos.core;
import eos.lighting;
import eos.math;
import eos.pass;
import eos.view;
import shadowCommon;

static const uint CASCADES = kCascadeCount;
static const float kCascadeOverlapNDC = 0.03;

[Pass]
[DispatchThreads(1, 1, 1)]
struct CascadeSetup
{
    [Input] View* view;
    [Input] DirectionalLight* sun;
    [Input] DepthReductionData* depthRange;
    [Output] CascadeData* cascades;

    bool useDepthReduction = true;      // otherwise the cascades span the camera's near to far plane
};

[[vk::push_constant]] CascadeSetup pass;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const View view = *pass.view;
    CascadeData cascades;

    float nearClip = view.nearPlane;
    float farClip = view.farPlane;
    float clipRange = farClip - nearClip;
    bool useDepthReduction = pass.useDepthReduction;
    float shadowResolution = float(kShadowMapSize);

    float sceneMin = nearClip;
    float sceneMax = nearClip + clipRange;

    if (useDepthReduction)
    {
        DepthReductionData reduction = *pass.depthRange;
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
    float3 lightForward = normalize(pass.sun.direction);

    [unroll]
    for (uint i = 0; i < CASCADES; ++i)
    {
        float splitDist = cascadeSplits[i];
        float cascadeNearDist = max(lastSplitDist - kCascadeOverlapNDC, 0.0);
        float cascadeFarDist = min(splitDist + kCascadeOverlapNDC, 1.0);

        float3 frustumCorners[8];
        frustumCorners[0] = UnprojectNDC(float3(-1.0,  1.0, 0.0), view.inverseViewProjection);
        frustumCorners[1] = UnprojectNDC(float3( 1.0,  1.0, 0.0), view.inverseViewProjection);
        frustumCorners[2] = UnprojectNDC(float3( 1.0, -1.0, 0.0), view.inverseViewProjection);
        frustumCorners[3] = UnprojectNDC(float3(-1.0, -1.0, 0.0), view.inverseViewProjection);
        frustumCorners[4] = UnprojectNDC(float3(-1.0,  1.0, 1.0), view.inverseViewProjection);
        frustumCorners[5] = UnprojectNDC(float3( 1.0,  1.0, 1.0), view.inverseViewProjection);
        frustumCorners[6] = UnprojectNDC(float3( 1.0, -1.0, 1.0), view.inverseViewProjection);
        frustumCorners[7] = UnprojectNDC(float3(-1.0, -1.0, 1.0), view.inverseViewProjection);

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

        cascades.viewProjection[i] = mul(lightView, lightOrtho);
        lastSplitDist = splitDist;
    }

    cascades.splits = float4(
        nearClip + cascadeSplits[0] * clipRange,
        nearClip + cascadeSplits[1] * clipRange,
        nearClip + cascadeSplits[2] * clipRange,
        nearClip + cascadeSplits[3] * clipRange
    );

    *pass.cascades = cascades;
}
