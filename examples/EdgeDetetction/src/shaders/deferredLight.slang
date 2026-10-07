// Deferred lighting as a fullscreen draw: shades the G-buffer with one directional light and a constant ambient term.
import eos.core;
import eos.math;
import eos.bsdf;
import eos.lighting;
import eos.fullscreen;
import eos.pass;
import eos.view;

enum DebugView
{
    Lit,
    Albedo,
    Normals,
    Roughness,
    WorldPosition,
};

[Pass]
struct DeferredLighting
{
    [Input] DescriptorHandle<Texture2D> albedo;
    [Input] DescriptorHandle<Texture2D> normal;
    [Input] DescriptorHandle<Texture2D> worldPos;
    [Input] View*                       view;
    DescriptorHandle<SamplerState>      linearSampler;

    DebugView debugView = DebugView.Lit;
    [Range(-1.0, 1.0)] float3 lightDirection;   // the direction the light travels
    [Range(0.0, 2.0)]  float  lightIntensity = 1.0;
    [Range(0.0, 1.0)]  float3 lightColor;
    [Range(0.0, 1.0)]  float  ambient = 0.3;    // radiance of the uniform environment that stands in for image-based lighting
};

[[vk::push_constant]] DeferredLighting pass;

struct Targets
{
    [Output] float4 output : SV_Target0;
};

[shader("fragment")]
Targets fragmentMain(FullscreenVaryings input)
{
    const float2 uv = input.uv;

    const float4 albedoMetallic = pass.albedo.Sample(pass.linearSampler, uv);
    const float4 normalRoughness = pass.normal.Sample(pass.linearSampler, uv);
    const float3 worldPos = pass.worldPos.Sample(pass.linearSampler, uv).xyz;

    const float3 albedo = albedoMetallic.rgb;
    const float3 normal = UnpackNormalUnorm(normalRoughness.rgb);
    const float roughness = saturate(normalRoughness.a);

    Targets targets;
    switch (pass.debugView)
    {
        case DebugView.Albedo:        targets.output = float4(albedo, 1.0); return targets;
        case DebugView.Normals:       targets.output = float4(normal * 0.5 + 0.5, 1.0); return targets;
        case DebugView.Roughness:     targets.output = roughness.xxxx; return targets;
        case DebugView.WorldPosition: targets.output = float4(abs(worldPos) * 0.05, 1.0); return targets;
        default: break;
    }

    const StandardBSDF bsdf = StandardBSDF(albedo, albedoMetallic.a, roughness);
    const ShadingFrame frame = ShadingFrame.FromNormal(normal);
    const float3 viewDirection = normalize(pass.view.position - worldPos);

    DirectionalLight sun;
    sun.direction = normalize(pass.lightDirection);
    sun.radiance = pass.lightColor * pass.lightIntensity;

    const float3 color = EvaluateDirectionalLight(bsdf, frame, viewDirection, sun)
                       + EvaluateConstantAmbient(bsdf, frame, viewDirection, pass.ambient.xxx, 1.0);
    targets.output = float4(color, 1.0);
    return targets;
}
