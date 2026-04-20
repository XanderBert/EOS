import bindings;
import shading;
import shadowCommon;
import math;

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

static const uint CASCADES = 4;
static const float kCascadeBlendDistance = 1.8;
static const float SHADOW_VALUE = 0.3;
static const float SHADOW_BIAS = 0.0009;
static const float2 SHADOW_MAP_SIZE = float2(4096.0, 4096.0);
static const bool PCF_ENABLED = true;

uint SelectCascade(float viewDepth, float4 cascadeSplits)
{
    if (viewDepth < cascadeSplits.x) { return 0; }
    if (viewDepth < cascadeSplits.y) { return 1; }
    if (viewDepth < cascadeSplits.z) { return 2; }
    return 3;
}

float3 CascadeDebugColor(uint cascadeIndex)
{
    if (cascadeIndex == 0) return float3(1.0, 0.0, 0.0);
    if (cascadeIndex == 1) return float3(0.0, 1.0, 0.0);
    if (cascadeIndex == 2) return float3(0.0, 0.0, 1.0);
    return float3(1.0, 0.0, 1.0);
}

struct CascadeShadowSample
{
    float shadow;
    bool isValid;
    float2 shadowUV;
    float receiverDepth;
    float sampledDepth;
    float depthDelta;
};

CascadeShadowSample SampleCascadeShadow(PerFrameData frameData, float3 worldPos, float ndotl, uint cascadeIndex)
{
    CascadeShadowSample result;

    // Transform the world position into the light's clip space to get the shadow map UVs and depth.
    float4 shadowCoord = mul(float4(worldPos, 1.0), frameData.cascadeViewProj[cascadeIndex]);
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;

    // Transform from [-1, 1] clip space to [0, 1] UV space.
    result.shadowUV = projCoords.xy * 0.5 + 0.5;

    // Flip the Y coordinate because Vulkan
    result.shadowUV.y = 1.0 - result.shadowUV.y;

    // Check if the projected coordinates are within the valid range for sampling the shadow map.
    bool inDepthRange = projCoords.z > 0.0 && projCoords.z < 1.0;

    // Check if the UV coordinates are within the [0, 1] range to ensure we're sampling within the shadow map.
    bool inUVRange = result.shadowUV.x > 0.0 && result.shadowUV.x < 1.0 && result.shadowUV.y > 0.0 && result.shadowUV.y < 1.0;
    result.isValid = inDepthRange && inUVRange;

    result.receiverDepth = projCoords.z;
    result.sampledDepth = TextureBindless2DArray(frameData.shadowMapID, 1, float3(result.shadowUV, cascadeIndex)).r;
    result.depthDelta = result.receiverDepth - result.sampledDepth;

    if (!result.isValid)
    {
        result.shadow = 1.0;
        return result;
    }

    // Add a bias to reduce shadow acne
    float cascadeScale = 1.0 - float(cascadeIndex) * 0.0015;
    float bias = ndotl * SHADOW_BIAS * cascadeScale;

    if(!PCF_ENABLED)
    {
        result.shadow = result.receiverDepth - bias > result.sampledDepth ? SHADOW_VALUE : 1.0;
        return result;
    }

    result.shadow = SamplePCF(frameData.shadowMapID, result.shadowUV, result.receiverDepth, bias, cascadeIndex);
    return result;
}

float SamplePCF(uint shadowMapID, float2 shadowUV, float receiverDepth, float bias, uint cascadeIndex)
{
    float shadow = 0.0;
    float2 texelSize = 1.0 / SHADOW_MAP_SIZE;

    // Perform a 3x3 PCF sampling around the shadow UV to smooth out the shadow edges.
    for (int x  = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            float pcfDepth = TextureBindless2DArray(shadowMapID, 1, float3(shadowUV + offset, cascadeIndex)).r;
            shadow += receiverDepth - bias > pcfDepth ? SHADOW_VALUE : 1.0;
        }
    }

    return  shadow / 9.0;
}

float ComputeInlineRayShadow(PerFrameData frameData, float3 worldPos, float3 normal, float3 lightDir)
{
    if (!IsFiniteFloat3(worldPos) || !IsFiniteFloat3(normal) || !IsFiniteFloat3(lightDir)) return 1.0;

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

    return (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? SHADOW_VALUE : 1.0;
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


    // Create Directional Light
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

    if (frameData.shadowForceCascade >= 0 && frameData.shadowForceCascade < 4) cascadeIndex = (uint)frameData.shadowForceCascade;

    uint baseCascade = cascadeIndex;
    uint nextCascade = cascadeIndex;
    float blendToNext = 0.0;

    if (frameData.shadowForceCascade < 0)
    {
        [unroll]
        for (uint splitIndex = 0; splitIndex < CASCADES - 1; ++splitIndex)
        {
            // Get the depth of where the split between cascades occurs in view space.
            // If the current pixel is within a certain distance from the split, blend between the two cascades to reduce popping.
            float splitDepth = frameData.cascadeSplits[splitIndex];
            if (abs(viewDepth - splitDepth) <= kCascadeBlendDistance)
            {
                baseCascade = splitIndex;
                nextCascade = splitIndex + 1;
                blendToNext = smoothstep(splitDepth - kCascadeBlendDistance, splitDepth + kCascadeBlendDistance, viewDepth);
                break;
            }
        }
    }

    // Debug visualization of cascades
    if (debugMode == 1)
    {
        float3 baseDebugColor = CascadeDebugColor(baseCascade);
        if (nextCascade != baseCascade)
        {
            float3 nextDebugColor = CascadeDebugColor(nextCascade);
            return float4(color * lerp(baseDebugColor, nextDebugColor, blendToNext), 1.0);
        }
        return float4(color * baseDebugColor, 1.0);
    }

    CascadeShadowSample baseSample = SampleCascadeShadow(frameData, input.WorldPos, ndotl, baseCascade);
    CascadeShadowSample activeSample = baseSample;

    float shadow = baseSample.shadow;
    if (nextCascade != baseCascade)
    {
        CascadeShadowSample nextSample = SampleCascadeShadow(frameData, input.WorldPos, ndotl, nextCascade);
        if (baseSample.isValid && nextSample.isValid)
        {
            shadow = lerp(baseSample.shadow, nextSample.shadow, blendToNext);
            activeSample = blendToNext < 0.5 ? baseSample : nextSample;
        }
        else if (nextSample.isValid)
        {
            shadow = nextSample.shadow;
            activeSample = nextSample;
        }
    }

    // Show Shadow UV
    if (debugMode == 2) return float4(saturate(activeSample.shadowUV), saturate(activeSample.receiverDepth), 1.0);

    // Show receiver depth in splits
    if (debugMode == 3) return float4(activeSample.receiverDepth.xxx, 1.0);

    // Show sampled depth
    if (debugMode == 4) return float4(activeSample.sampledDepth.xxx, 1.0);

    // Show depth delta between receiver depth and sampled depth.
    // This can be used to debug shadow acne and peter panning issues.
    // Green areas indicate potential shadow acne (receiver depth is close to or less than sampled depth)
    // while red areas indicate potential peter panning (receiver depth is much greater than sampled depth).
    if (debugMode == 5)
    {
        float d = saturate(activeSample.depthDelta * 80.0 + 0.5);
        return float4(d, 1.0 - d, 0.0, 1.0);
    }

    // Show whether the shadow sample is valid (within the light's frustum).
    // This can help visualize the cascade splits and debug issues with shadow map coverage.
    if (debugMode == 6) return activeSample.isValid ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);

    //If the coord is outside the light frustum don't shadow it.
    if (!activeSample.isValid) return float4(color, 1.0);

    return float4(color * shadow, 1.0);
}