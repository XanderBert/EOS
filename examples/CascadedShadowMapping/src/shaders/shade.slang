// Shades the surfaces the early-Z pass kept (depth test Equal, so each pixel once) with the sun and a constant ambient
// term. Shadows come from the cascaded shadow map, blended across cascade splits, or from ray queries against the
// scene's TLAS; the debug views show how the cascades cover the scene.
import eos.core;
import eos.math;
import eos.bindless;
import eos.material;
import eos.lighting;
import eos.pass;
import eos.scene;
import eos.view;
import shadowCommon;

enum Shadows
{
    Cascades,               // the shadow map and its cascades
    RayQuery,               // a ray towards the sun per pixel, against the scene's TLAS
};

enum DebugView
{
    Shading,
    CascadeIndex,
    ShadowUV,
    ReceiverDepth,
    ShadowMapDepth,
    DepthDelta,
    Validity,
};

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
struct Shade
{
    [Input] Scene* scene;
    [Input] View* view;
    [Input] DirectionalLight* sun;
    [Input] [DepthTest(CompareOp.Equal)] DepthTarget depth;             // the early-Z pass's
    [Input] [Optional] CascadeData* cascades;                            // Cascades only
    [Input] [Optional] DescriptorHandle<Texture2DArray> shadowMap;       // Cascades only
    [Sampler(Filter.Linear, Address.ClampToBorder)] DescriptorHandle<SamplerState> shadowSampler;

    Shadows shadows = Shadows.Cascades;
    DebugView debugView = DebugView.Shading;
    [Range(-1, 3)] int forceCascade = -1;           // a cascade every pixel uses; -1 picks by distance
    [Range(0.0, 1.0)] float ambient = 0.3;          // radiance of the uniform environment that stands in for image-based lighting
};

[[vk::push_constant]] Shade pass;

struct VSOutput
{
    float4 position                 : SV_Position;
    float3 worldPosition            : TEXCOORD1;
    float3 normal                   : TEXCOORD2;
    float4 tangent                  : TANGENT;
    float2 uv                       : TEXCOORD0;
    nointerpolation uint instance   : TEXCOORD3;
};

struct Targets
{
    [Output] [Clear(0.36, 0.4, 1.0, 0.28)] float4 color : SV_Target0;
};

static const uint CASCADES = kCascadeCount;
static const float kCascadeBlendDistance = 1.8;
static const float SHADOW_BIAS = 0.0009;
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
    float visibility;       // 1 lit, 0 fully shadowed
    bool isValid;
    float2 shadowUV;
    float receiverDepth;
    float sampledDepth;
    float depthDelta;
};

float SampleShadowDepth(float2 shadowUV, uint cascadeIndex)
{
    // The shadow map has a single mip; an explicit level keeps this valid in non-uniform control flow.
    return pass.shadowMap.SampleLevel(pass.shadowSampler, float3(shadowUV, cascadeIndex), 0.0).r;
}

CascadeShadowSample SampleCascadeShadow(float3 worldPos, float ndotl, uint cascadeIndex)
{
    CascadeShadowSample result;

    // Transform the world position into the light's clip space to get the shadow map UVs and depth.
    float4 shadowCoord = mul(float4(worldPos, 1.0), pass.cascades.viewProjection[cascadeIndex]);
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
    result.sampledDepth = SampleShadowDepth(result.shadowUV, cascadeIndex);
    result.depthDelta = result.receiverDepth - result.sampledDepth;

    if (!result.isValid)
    {
        result.visibility = 1.0;
        return result;
    }

    // Add a bias to reduce shadow acne
    float cascadeScale = 1.0 - float(cascadeIndex) * 0.0015;
    float bias = ndotl * SHADOW_BIAS * cascadeScale;

    if (!PCF_ENABLED)
    {
        result.visibility = result.receiverDepth - bias > result.sampledDepth ? 0.0 : 1.0;
        return result;
    }

    result.visibility = SamplePCF(result.shadowUV, result.receiverDepth, bias, cascadeIndex);
    return result;
}

float SamplePCF(float2 shadowUV, float receiverDepth, float bias, uint cascadeIndex)
{
    float visibility = 0.0;
    float2 texelSize = 1.0 / float2(kShadowMapSize, kShadowMapSize);

    // Perform a 3x3 PCF sampling around the shadow UV to smooth out the shadow edges.
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            float pcfDepth = SampleShadowDepth(shadowUV + offset, cascadeIndex);
            visibility += receiverDepth - bias > pcfDepth ? 0.0 : 1.0;
        }
    }

    return visibility / 9.0;
}

float ComputeInlineRayVisibility(float3 worldPos, float3 normal, float3 lightDir)
{
    if (!IsValid(pass.scene.tlas)) return 1.0;
    if (!IsFiniteFloat3(worldPos) || !IsFiniteFloat3(normal) || !IsFiniteFloat3(lightDir)) return 1.0;

    float3 rayDir = SafeNormalize(lightDir, float3(0.0, -1.0, 0.0));
    float3 rayNormal = SafeNormalize(normal, float3(0.0, 1.0, 0.0));

    float maxTraceDistance = max(pass.view.farPlane, 1.0) * 2.0;

    RayDesc ray;
    ray.Origin = worldPos + rayNormal * 0.01;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = max(maxTraceDistance, 1.0);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> rayQuery;
    rayQuery.TraceRayInline(pass.scene.tlas, RAY_FLAG_NONE, 0xFF, ray);

    while (rayQuery.Proceed()) {}

    return (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
}

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    const MeshVertex vertex = pass.scene.vertices[vertexID];
    const float4x4 transform = pass.scene.instances[instanceID].transform;
    const float4 worldPosition = mul(float4(vertex.position, 1.0), transform);
    const float3x3 normalMatrix = float3x3(transform);

    VSOutput output;
    output.position      = mul(worldPosition, pass.view.viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal        = mul(vertex.normal, normalMatrix);
    output.tangent       = float4(mul(vertex.tangent.xyz, normalMatrix), vertex.tangent.w);
    output.uv            = vertex.uv;
    output.instance      = instanceID;
    return output;
}

[shader("fragment")]
Targets fragmentMain(VSOutput input)
{
    Targets targets;
    targets.color = ShadePixel(input);
    return targets;
}

float4 ShadePixel(VSOutput input)
{
    const SurfaceData surface = SurfaceData.FromVertex(input.worldPosition, input.normal, input.tangent, input.uv, pass.view.position);
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    // Directional light: its direction is the direction the light travels.
    DirectionalLight sun = *pass.sun;
    sun.direction = SafeNormalize(sun.direction, float3(0.0, -1.0, 0.0));

    const float3 direct = EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun);
    const float3 indirect = EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, pass.ambient.xxx, point.occlusion) + point.emission;
    const float3 N = point.frame.N;
    const float3 L = -sun.direction;
    const float ndotl = saturate(dot(N, L));

    if (pass.shadows == Shadows.RayQuery)
    {
        const float visibility = ComputeInlineRayVisibility(input.worldPosition, N, L);
        return float4(direct * visibility + indirect, 1.0);
    }

    const float4 viewPos = mul(float4(input.worldPosition, 1.0), pass.view.view);
    const float viewDepth = -viewPos.z;
    const float4 cascadeSplits = pass.cascades.splits;
    uint cascadeIndex = SelectCascade(viewDepth, cascadeSplits);
    const DebugView debugView = pass.debugView;

    if (pass.forceCascade >= 0 && pass.forceCascade < CASCADES) cascadeIndex = (uint)pass.forceCascade;

    uint baseCascade = cascadeIndex;
    uint nextCascade = cascadeIndex;
    float blendToNext = 0.0;

    if (pass.forceCascade < 0)
    {
        [unroll]
        for (uint splitIndex = 0; splitIndex < CASCADES - 1; ++splitIndex)
        {
            // Get the depth of where the split between cascades occurs in view space.
            // If the current pixel is within a certain distance from the split, blend between the two cascades to reduce popping.
            float splitDepth = cascadeSplits[splitIndex];
            if (abs(viewDepth - splitDepth) <= kCascadeBlendDistance)
            {
                baseCascade = splitIndex;
                nextCascade = splitIndex + 1;
                blendToNext = smoothstep(splitDepth - kCascadeBlendDistance, splitDepth + kCascadeBlendDistance, viewDepth);
                break;
            }
        }
    }

    const float3 color = direct + indirect;

    // Debug visualization of cascades
    if (debugView == DebugView.CascadeIndex)
    {
        float3 baseDebugColor = CascadeDebugColor(baseCascade);
        if (nextCascade != baseCascade)
        {
            float3 nextDebugColor = CascadeDebugColor(nextCascade);
            return float4(color * lerp(baseDebugColor, nextDebugColor, blendToNext), 1.0);
        }
        return float4(color * baseDebugColor, 1.0);
    }

    CascadeShadowSample baseSample = SampleCascadeShadow(input.worldPosition, ndotl, baseCascade);
    CascadeShadowSample activeSample = baseSample;

    float visibility = baseSample.visibility;
    if (nextCascade != baseCascade)
    {
        CascadeShadowSample nextSample = SampleCascadeShadow(input.worldPosition, ndotl, nextCascade);
        if (baseSample.isValid && nextSample.isValid)
        {
            visibility = lerp(baseSample.visibility, nextSample.visibility, blendToNext);
            activeSample = blendToNext < 0.5 ? baseSample : nextSample;
        }
        else if (nextSample.isValid)
        {
            visibility = nextSample.visibility;
            activeSample = nextSample;
        }
    }

    // Show Shadow UV
    if (debugView == DebugView.ShadowUV) return float4(saturate(activeSample.shadowUV), saturate(activeSample.receiverDepth), 1.0);

    // Show receiver depth in splits
    if (debugView == DebugView.ReceiverDepth) return float4(activeSample.receiverDepth.xxx, 1.0);

    // Show sampled depth
    if (debugView == DebugView.ShadowMapDepth) return float4(activeSample.sampledDepth.xxx, 1.0);

    // Show depth delta between receiver depth and sampled depth.
    // This can be used to debug shadow acne and peter panning issues.
    // Green areas indicate potential shadow acne (receiver depth is close to or less than sampled depth)
    // while red areas indicate potential peter panning (receiver depth is much greater than sampled depth).
    if (debugView == DebugView.DepthDelta)
    {
        float d = saturate(activeSample.depthDelta * 80.0 + 0.5);
        return float4(d, 1.0 - d, 0.0, 1.0);
    }

    // Show whether the shadow sample is valid (within the light's frustum).
    // This can help visualize the cascade splits and debug issues with shadow map coverage.
    if (debugView == DebugView.Validity) return activeSample.isValid ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);

    // If the coord is outside the light frustum don't shadow it.
    if (!activeSample.isValid) return float4(color, 1.0);

    return float4(direct * visibility + indirect, 1.0);
}
