import bindings;
import shading;
import shadowCommon;


struct MaterialHandles
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
}

struct VSInput
{
    uint vertexID   : SV_VertexID;
    float3 Pos      : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    float4 Tangent  : TANGENT;
};

struct VSOutput
{
    float4 Pos      : SV_Position;
    float3 WorldPos : TEXCOORD1;
    float3 Normal   : TEXCOORD2;
    float3 Tangent  : TANGENT;
    float2 UV       : TEXCOORD0;
    uint   DrawIndex : TEXCOORD3;
};


uint SelectCascade(float viewDepth, float4 cascadeSplits)
{
    if (viewDepth < cascadeSplits.x) { return 0; }
    if (viewDepth < cascadeSplits.y) { return 1; }
    if (viewDepth < cascadeSplits.z) { return 2; }
    return 3;
}

// Runtime debug modes from PerFrameData.shadowDebugMode:
// 0 = normal shading
// 1 = cascade index color
// 2 = shadow UV visualization
// 3 = receiver shadow depth (proj z)
// 4 = sampled shadow-map depth
// 5 = depth delta heatmap (receiver - map)
// 6 = validity mask (inside light frustum)

float3 CascadeDebugColor(uint cascadeIndex)
{
    if (cascadeIndex == 0) return float3(1.0, 0.0, 0.0);
    if (cascadeIndex == 1) return float3(0.0, 1.0, 0.0);
    if (cascadeIndex == 2) return float3(0.0, 0.0, 1.0);
    return float3(1.0, 0.0, 1.0);
}

bool IsFiniteFloat3(float3 v)
{
    return all(!isnan(v)) && all(!isinf(v));
}

float3 SafeNormalize(float3 v, float3 fallback)
{
    float lenSq = dot(v, v);
    if (lenSq <= 1e-12 || !IsFiniteFloat3(v))
    {
        return fallback;
    }

    return v * rsqrt(lenSq);
}

float ComputeInlineRayShadow(PerFrameData frameData, float3 worldPos, float3 normal, float3 lightDir)
{
    if (!IsFiniteFloat3(worldPos) || !IsFiniteFloat3(normal) || !IsFiniteFloat3(lightDir))
    {
        return 1.0;
    }

    float3 rayDir = SafeNormalize(lightDir, float3(0.0, -1.0, 0.0));
    float3 rayNormal = SafeNormalize(normal, float3(0.0, 1.0, 0.0));

    RaytracingAccelerationStructure sceneTLAS = GetAccelerationStructure(frameData.sceneTLASID);
    float maxTraceDistance = max(frameData.cascadeSplits.w, 1.0) * 2.0;

    RayDesc ray;
    ray.Origin = worldPos + rayNormal * 0.01;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = max(maxTraceDistance, 1.0);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;
    rayQuery.TraceRayInline(sceneTLAS, RAY_FLAG_NONE, 0xFF, ray);

    while (rayQuery.Proceed()) {}

    return (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.3 : 1.0;
}

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, drawIndex);

    float4 localTransformed = mul(float4(input.Pos, 1.0), draw.transform);
    float3x3 normalMatrix   = float3x3(mul(draw.transform, frameData.Model));

    VSOutput output;
    output.Pos          = mul(localTransformed, frameData.MVP);
    output.WorldPos     = mul(localTransformed, frameData.Model).xyz;
    output.Normal       = normalize(mul(input.Normal, normalMatrix));
    output.Tangent      = mul(input.Tangent.xyz, normalMatrix);
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    // Sample textures
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, input.DrawIndex);

    MaterialTextureSet materialTextures;
    materialTextures.albedo = draw.albedo;
    materialTextures.normal = draw.normal;
    materialTextures.metallicRoughness = draw.metallicRoughness;

    PBRShadingInput pbrInput;
    pbrInput.worldPos = input.WorldPos;
    pbrInput.geometricNormal = input.Normal;
    pbrInput.tangent = input.Tangent;
    pbrInput.uv = input.UV;
    pbrInput.cameraPos = frameData.cameraPos;

    float3 directionalLightDir = SafeNormalize(-frameData.lightDir.xyz, float3(0.0, -1.0, 0.0));
    pbrInput.lightPos = input.WorldPos + directionalLightDir * 100000.0;

    Material material = SampleMaterial(materialTextures, 0, input.UV);
    float3 shadingNormal = SampleNormal(materialTextures, 0, input.UV, input.Normal, input.WorldPos, input.Tangent);
    PBRSurface pbrSurface = BuildPBRSurface(pbrInput, material, shadingNormal);
    pbrSurface.lightDir = directionalLightDir;
    float3 color = EvaluatePBR(pbrSurface);

    float3 N = pbrSurface.normal;
    float3 L = directionalLightDir;
    float ndotl = saturate(dot(N, L));

    if (frameData.shadowTechnique == SHADOW_TECHNIQUE_RAY_QUERY)
    {
        float3 shadowLightDir = SafeNormalize(-frameData.lightDir.xyz, L);
        float shadow = ComputeInlineRayShadow(frameData, input.WorldPos, N, shadowLightDir);
        return float4(color * shadow, 1.0);
    }


    float4 viewPos = mul(float4(input.WorldPos, 1.0), frameData.View);
    float viewDepth = -viewPos.z;
    uint cascadeIndex = SelectCascade(viewDepth, frameData.cascadeSplits);
    int debugMode = frameData.shadowDebugMode;

    if (frameData.shadowForceCascade >= 0 && frameData.shadowForceCascade < 4)
    {
        cascadeIndex = (uint)frameData.shadowForceCascade;
    }

    if (debugMode == 1)
    {
        return float4(color * CascadeDebugColor(cascadeIndex), 1.0);
    }

    float4 shadowCoord = mul(float4(input.WorldPos, 1.0), frameData.cascadeViewProj[cascadeIndex]);
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;

    float2 shadowUV = projCoords.xy * 0.5 + 0.5;
    // EOS uses a flipped Vulkan viewport, so shadow-map UV Y must be flipped.
    shadowUV.y = 1.0 - shadowUV.y;

    bool inDepthRange = projCoords.z > 0.0 && projCoords.z < 1.0;
    bool inUVRange = shadowUV.x > 0.0 && shadowUV.x < 1.0 && shadowUV.y > 0.0 && shadowUV.y < 1.0;
    bool isValidShadowCoord = inDepthRange && inUVRange;

    float sampledDepth = TextureBindless2DArray(frameData.shadowMapID, 1, float3(shadowUV, cascadeIndex)).r;
    float depthDelta = projCoords.z - sampledDepth;

    if (debugMode == 2)
    {
        return float4(saturate(shadowUV), saturate(projCoords.z), 1.0);
    }
    if (debugMode == 3)
    {
        return float4(projCoords.zzz, 1.0);
    }
    if (debugMode == 4)
    {
        return float4(sampledDepth.xxx, 1.0);
    }
    if (debugMode == 5)
    {
        float d = saturate(depthDelta * 80.0 + 0.5);
        return float4(d, 1.0 - d, 0.0, 1.0);
    }
    if (debugMode == 6)
    {
        return isValidShadowCoord ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
    }

    //If the coord is outside the light frustum don't shadow it.
    if (!isValidShadowCoord)
    {
        return float4(color, 1.0);
    }


    //TODO: Interpolate this scale as now, this will have a hard change while changing cascade
    float cascadeScale = 1.0 - float(cascadeIndex) * 0.0015;
    float bias = ndotl * 0.009 * cascadeScale;

    float currentDepth = projCoords.z;
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(4096.0, 4096.0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x,y) * texelSize;
            float pcfDepth = TextureBindless2DArray(frameData.shadowMapID, 1, float3(shadowUV + offset, cascadeIndex)).r;
            shadow += currentDepth - bias > pcfDepth ? 0.3 : 1.0;
        }
    }
    shadow /= 9.0;

    return float4(color * shadow, 1.0);
}