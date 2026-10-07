// Deferred lighting: shades the G-buffer with one directional light and a constant ambient term.
import eos.core;
import eos.math;
import eos.bsdf;
import eos.lighting;
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
    [Input] DescriptorHandle<Texture2D>         albedo;
    [Input] DescriptorHandle<Texture2D>         normal;
    [Input] DescriptorHandle<Texture2D>         worldPos;
    [Input] View*                               view;
    [Output] DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<SamplerState>              linearSampler;

    DebugView debugView = DebugView.Lit;
    [Range(-1.0, 1.0)] float3 lightDirection;   // the direction the light travels
    [Range(0.0, 2.0)]  float  lightIntensity = 1.0;
    [Range(0.0, 1.0)]  float3 lightColor;
    [Range(0.0, 1.0)]  float  ambient = 0.3;    // radiance of the uniform environment that stands in for image-based lighting
};

[[vk::push_constant]] DeferredLighting pass;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const RWTexture2D<float4> outputImage = pass.output;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);

    const uint2 pixelCoord = dispatchThreadID.xy;
    if (any(pixelCoord >= imageSize)) return;

    const float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);

    const float4 albedoMetallic = pass.albedo.SampleLevel(pass.linearSampler, uv, 0.0);
    const float4 normalRoughness = pass.normal.SampleLevel(pass.linearSampler, uv, 0.0);
    const float3 worldPos = pass.worldPos.SampleLevel(pass.linearSampler, uv, 0.0).xyz;

    const float3 albedo = albedoMetallic.rgb;
    const float3 normal = UnpackNormalUnorm(normalRoughness.rgb);
    const float roughness = saturate(normalRoughness.a);

    float4 result;
    switch (pass.debugView)
    {
        case DebugView.Albedo:        result = float4(albedo, 1.0); break;
        case DebugView.Normals:       result = float4(normal * 0.5 + 0.5, 1.0); break;
        case DebugView.Roughness:     result = roughness.xxxx; break;
        case DebugView.WorldPosition: result = float4(abs(worldPos) * 0.05, 1.0); break;
        default:
        {
            const StandardBSDF bsdf = StandardBSDF(albedo, albedoMetallic.a, roughness);
            const ShadingFrame frame = ShadingFrame.FromNormal(normal);
            const float3 viewDirection = normalize(pass.view.position - worldPos);

            DirectionalLight sun;
            sun.direction = normalize(pass.lightDirection);
            sun.radiance = pass.lightColor * pass.lightIntensity;

            const float3 color = EvaluateDirectionalLight(bsdf, frame, viewDirection, sun)
                               + EvaluateConstantAmbient(bsdf, frame, viewDirection, pass.ambient.xxx, 1.0);
            result = float4(color, 1.0);
            break;
        }
    }

    outputImage[pixelCoord] = result;
}
