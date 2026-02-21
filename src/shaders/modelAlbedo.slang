import common;
import pbr;

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    MaterialHandles material;
};

struct PushConstants
{
    uint64_t PerFramePointer;
};
[[vk::push_constant]] PushConstants pc;


[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    PerFrameData frameData = loadBuffer<PerFrameData>(pc.PerFramePointer);

    VSOutput output;
    output.Pos = mul(float4(input.Pos, 1.0), frameData.MVP);
    output.Normal = mul((float3x3)frameData.Model, input.Normal);
    output.UV = input.UV;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    PerFrameData frameData = loadBuffer<PerFrameData>(pc.PerFramePointer);

    // Sample textures
    float4 albedoSample           = textureBindless2D(frameData.material.albedo, 0, input.UV);
    float4 metallicRoughnessSample = textureBindless2D(frameData.material.metallicRoughness, 0, input.UV);

    //R=occlusion, G=roughness, B=metallic
    float roughness = metallicRoughnessSample.g;
    float metallic  = metallicRoughnessSample.b;

    Material mat;
    mat.r         = albedoSample.r;
    mat.g         = albedoSample.g;
    mat.b         = albedoSample.b;
    mat.metallic  = metallic;
    mat.roughness = roughness;

    float3 cameraPos = float3(0, 0, -3.5);
    float3 N = normalize(input.Normal);
    float3 V = normalize(cameraPos - input.Pos.xyz);

    // Specular contribution
    // Simple directional light pointing down-right
    float3 L = normalize(float3(1.0, 1.0, 1.0));
    float3 Lo = BRDF(L, V, N, mat);

    // Combine with ambient
    float3 color = float3(mat.r, mat.g, mat.b) * 0.02;
    color += Lo;

    // Gamma correct
    color = pow(color, float3(0.4545, 0.4545, 0.4545));

    return float4(color, 1.0);
}