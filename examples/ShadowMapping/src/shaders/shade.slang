// Shades the scene with the sun, shadowed by the shadow map (3x3 PCF), and a constant ambient term.
import eos.core;
import eos.math;
import eos.material;
import eos.lighting;
import eos.pass;
import eos.scene;
import eos.view;

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
struct Shade
{
    [Input] Scene* scene;
    [Input] View* view;
    [Input] View* lightView;                        // the View the shadow map was rendered with
    [Input] DirectionalLight* sun;
    [Input] DescriptorHandle<Texture2D> shadowMap;
    [Sampler(Filter.Linear, Address.ClampToBorder)] DescriptorHandle<SamplerState> shadowSampler;
    [Output] [Format("Z_F32")] DepthTarget depth;

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
    float4 shadowCoord              : TEXCOORD4;
};

struct Targets
{
    [Output] [Clear(0.36, 0.4, 1.0, 0.28)] float4 color : SV_Target0;
};

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
    output.shadowCoord   = mul(worldPosition, pass.lightView.viewProjection);
    return output;
}

// Fraction of the light that reaches the point: 1 lit, 0 fully shadowed. 3x3 PCF.
float SampleShadowVisibility(float4 shadowCoord, float3 geometricNormal, float3 lightDirection)
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

    uint2 shadowMapSize;
    pass.shadowMap.GetDimensions(shadowMapSize.x, shadowMapSize.y);
    const float2 texelSize = 1.0 / float2(shadowMapSize);

    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            const float occluderDepth = pass.shadowMap.Sample(pass.shadowSampler, projCoords.xy + float2(x, y) * texelSize).r;
            visibility += projCoords.z - bias > occluderDepth ? 0.0 : 1.0;
        }
    }

    return visibility / 9.0;
}

[shader("fragment")]
Targets fragmentMain(VSOutput input)
{
    const SurfaceData surface = SurfaceData.FromVertex(input.worldPosition, input.normal, input.tangent, input.uv, pass.view.position);
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    DirectionalLight sun = *pass.sun;
    sun.direction = SafeNormalize(sun.direction, float3(0.0, -1.0, 0.0));

    const float visibility = SampleShadowVisibility(input.shadowCoord, surface.geometricNormal, sun.direction);

    Targets targets;
    targets.color = float4(EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun) * visibility
                         + EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, pass.ambient.xxx, point.occlusion)
                         + point.emission, 1.0);
    return targets;
}
