import eos.core;
import eos.math;
import eos.bsdf;
import eos.lighting;

[CppExport]
public struct DeferredLightingPC
{
    public DescriptorHandle<Texture2D>           gbufferAlbedo;
    public DescriptorHandle<Texture2D>           gbufferNormal;
    public DescriptorHandle<Texture2D>           gbufferWorldPos;
    public DescriptorHandle<SamplerState>        samplerState;
    public DescriptorHandle<RWTexture2D<float4>> outputImage;
    public uint   debugView;
    public float4 cameraPos;
    public float4 lightDirIntensity;    // xyz: direction the light travels, w: intensity
    public float4 lightColorAmbient;    // rgb: light color, w: ambient radiance
};

[[vk::push_constant]] DeferredLightingPC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const RWTexture2D<float4> outputImage = pc.outputImage;
    uint2 imageSize;
    outputImage.GetDimensions(imageSize.x, imageSize.y);

    const uint2 pixelCoord = dispatchThreadID.xy;
    if (any(pixelCoord >= imageSize)) return;

    const float2 uv = (float2(pixelCoord) + 0.5) / float2(imageSize);

    const float4 albedoMetallic = pc.gbufferAlbedo.SampleLevel(pc.samplerState, uv, 0.0);
    const float4 normalRoughness = pc.gbufferNormal.SampleLevel(pc.samplerState, uv, 0.0);
    const float3 worldPos = pc.gbufferWorldPos.SampleLevel(pc.samplerState, uv, 0.0).xyz;

    const float3 albedo = albedoMetallic.rgb;
    const float3 normal = UnpackNormalUnorm(normalRoughness.rgb);
    const float roughness = saturate(normalRoughness.a);

    float4 result;
    if (pc.debugView == 1)
    {
        result = float4(albedo, 1.0);
    }
    else if (pc.debugView == 2)
    {
        result = float4(normal * 0.5 + 0.5, 1.0);
    }
    else if (pc.debugView == 3)
    {
        result = roughness.xxxx;
    }
    else if (pc.debugView == 4)
    {
        result = float4(abs(worldPos) * 0.05, 1.0);
    }
    else
    {
        const StandardBSDF bsdf = StandardBSDF(albedo, albedoMetallic.a, roughness);
        const ShadingFrame frame = ShadingFrame.FromNormal(normal);
        const float3 viewDirection = normalize(pc.cameraPos.xyz - worldPos);

        DirectionalLight sun;
        sun.direction = normalize(pc.lightDirIntensity.xyz);
        sun.radiance = pc.lightColorAmbient.rgb * pc.lightDirIntensity.w;

        const float3 color = EvaluateDirectionalLight(bsdf, frame, viewDirection, sun)
                           + EvaluateConstantAmbient(bsdf, frame, viewDirection, pc.lightColorAmbient.www, 1.0);
        result = float4(color, 1.0);
    }

    outputImage[pixelCoord] = result;
}
