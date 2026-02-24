import pbr;
import common;

public struct MaterialHandles
{
    public uint albedo;
    public uint normal;
    public uint metallicRoughness;
}


public struct VSInput
{
    public uint vertexID : SV_VertexID;
    public float3 Pos : POSITION;
    public float3 Normal;
    public float2 UV  : TEXCOORD0;
};

public struct VSOutput
{
    public float4 Pos : SV_Position;
    public float3 WorldPos : TEXCOORD1;
    public float3 Normal;
    public float2 UV  : TEXCOORD0;
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float3 cameraPos;
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
    output.WorldPos = mul(float4(input.Pos, 1.0), frameData.Model).xyz;
    output.Normal = mul(float4(input.Normal, 1.0), frameData.Model).xyz;
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

    float3 N = normalize(input.Normal);
    float3 V = normalize(frameData.cameraPos - input.WorldPos.xyz);

    // Specular contribution
    // Simple directional light pointing down-right
    float3 L = normalize(float3(1.0, 1.0, 1.0));
    float3 Lo = BRDF(L, V, N, mat);

    // Combine with ambient
    float3 color = float3(mat.r, mat.g, mat.b) * 0.3;
    color += Lo;

    // Gamma correct
    color = pow(color, float3(0.4545, 0.4545, 0.4545));

    return float4(color, 1.0);
}