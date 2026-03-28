import common;
import shadowCommon;

static const uint CASCADES = 4;

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

float3 UnprojectNDC(float3 ndc)
{
    float4 world = mul(float4(ndc, 1.0), (pc.invViewProjection));
    return world.xyz / world.w;
}

float4x4 BuildLookAtRH(float3 eye, float3 target, float3 up)
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

float4x4 BuildOrthoZO(float left, float right, float bottom, float top, float zNear, float zFar)
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
    float shadowResolution = max(pc.cameraPlanes.w, 1.0);

    float sceneMin = nearClip;
    float sceneMax = nearClip + clipRange;

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

        float3 frustumCorners[8];
        frustumCorners[0] = UnprojectNDC(float3(-1.0,  1.0, 0.0));
        frustumCorners[1] = UnprojectNDC(float3( 1.0,  1.0, 0.0));
        frustumCorners[2] = UnprojectNDC(float3( 1.0, -1.0, 0.0));
        frustumCorners[3] = UnprojectNDC(float3(-1.0, -1.0, 0.0));
        frustumCorners[4] = UnprojectNDC(float3(-1.0,  1.0, 1.0));
        frustumCorners[5] = UnprojectNDC(float3( 1.0,  1.0, 1.0));
        frustumCorners[6] = UnprojectNDC(float3( 1.0, -1.0, 1.0));
        frustumCorners[7] = UnprojectNDC(float3(-1.0, -1.0, 1.0));

        [unroll]
        for (uint j = 0; j < 4; ++j)
        {
            float3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + dist * splitDist;
            frustumCorners[j] = frustumCorners[j] + dist * lastSplitDist;
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