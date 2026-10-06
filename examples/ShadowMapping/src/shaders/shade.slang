import eos.math;
import eos.material;
import eos.lighting;
import shadowCommon;

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
    float4 Pos         : SV_Position;
    float3 WorldPos    : TEXCOORD1;
    float3 Normal      : TEXCOORD2;
    float4 Tangent     : TANGENT;
    float2 UV          : TEXCOORD0;
    uint   DrawIndex   : TEXCOORD3;
    float4 ShadowCoord : TEXCOORD4;
};

// Must match the shadow depth texture created in main.cpp.
static const float kShadowMapSize = 4096.0;

static const float3 kSunRadiance = float3(3.0);
static const float3 kAmbientRadiance = float3(0.3);

[[vk::push_constant]] FramePointers pc;

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    const PerFrameData frameData = *pc.perFrame;
    const float4x4 transform = pc.draws[drawIndex].transform;

    const float4 localTransformed = mul(float4(input.Pos, 1.0), transform);
    const float3x3 normalMatrix   = float3x3(mul(transform, frameData.model));

    VSOutput output;
    output.Pos          = mul(localTransformed, frameData.mvp);
    output.WorldPos     = mul(localTransformed, frameData.model).xyz;
    output.Normal       = mul(input.Normal, normalMatrix);
    output.Tangent      = float4(mul(input.Tangent.xyz, normalMatrix), input.Tangent.w);
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;
    output.ShadowCoord  = mul(localTransformed, frameData.depthMVP);
    return output;
}

// Fraction of the light that reaches the point: 1 lit, 0 fully shadowed. 3x3 PCF.
float SampleShadowVisibility(PerFrameData frameData, float4 shadowCoord, float3 geometricNormal, float3 lightDirection)
{
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;

    // Only remap XY -> Vulkan depth already stays in [0, 1].
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // EOS uses a flipped Vulkan viewport, so shadow-map UV Y must be flipped.
    projCoords.y = 1.0 - projCoords.y;

    // Outside the light frustum nothing is shadowed (the border colour would read as occluded).
    if (projCoords.z > 1.0 || any(projCoords.xy < 0.0) || any(projCoords.xy > 1.0)) return 1.0;

    // Slope bias from the geometric normal; normal-map detail shouldn't move the depth comparison.
    const float ndotl = saturate(dot(geometricNormal, -lightDirection));
    const float bias = max(0.0005 * (1.0 - ndotl), 0.00005);
    const float2 texelSize = 1.0 / float2(kShadowMapSize, kShadowMapSize);

    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            const float occluderDepth = frameData.shadowMap.Sample(frameData.shadowSampler, projCoords.xy + float2(x, y) * texelSize).r;
            visibility += projCoords.z - bias > occluderDepth ? 0.0 : 1.0;
        }
    }

    return visibility / 9.0;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    const PerFrameData frameData = *pc.perFrame;
    const SurfaceData surface = SurfaceData.FromVertex(input.WorldPos, input.Normal, input.Tangent, input.UV, frameData.cameraPos);
    const StandardMaterial material = StandardMaterial(pc.draws[input.DrawIndex].material);
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    // The shadow map uses an orthographic projection, so the light is directional.
    DirectionalLight sun;
    sun.direction = SafeNormalize(frameData.lightDir.xyz, float3(0.0, -1.0, 0.0));
    sun.radiance = kSunRadiance;

    const float visibility = SampleShadowVisibility(frameData, input.ShadowCoord, surface.geometricNormal, sun.direction);
    const float3 color = EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun) * visibility
                       + EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, kAmbientRadiance, point.occlusion)
                       + point.emission;
    return float4(color, 1.0);
}
