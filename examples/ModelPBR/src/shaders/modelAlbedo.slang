import pbr;
import common;

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
};

struct VSOutput
{
    float4 Pos      : SV_Position;
    float3 WorldPos : TEXCOORD1;
    float3 Normal   : TEXCOORD2;
    float2 UV       : TEXCOORD0;
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
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    VSOutput output;
    output.Pos          = mul(float4(input.Pos, 1.0), frameData.MVP);
    output.WorldPos     = mul(float4(input.Pos, 1.0), frameData.Model).xyz;
    output.Normal       = normalize(mul(input.Normal, float3x3(frameData.Model)));
    output.UV           = input.UV;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    // Sample textures
    float3 albedoSample = TextureBindless2D(frameData.material.albedo, 0, input.UV).rgb;
    float2 normalSample = TextureBindless2D(frameData.material.normal, 0, input.UV).rg;
    float4 metallicRoughnessSample = TextureBindless2D(frameData.material.metallicRoughness, 0, input.UV);


    // Material properties
    Material material;
    material.albedo = albedoSample;
    material.metallic = metallicRoughnessSample.b;
    material.roughness = metallicRoughnessSample.g;

    float3 N = GetShadingNormal(input.Normal, input.WorldPos, normalSample, input.UV);
    float3 V = normalize(frameData.cameraPos - input.WorldPos);

    // simple PBR lighting
    float3 L = normalize(float3(1.0, 1.0, 1.0));
    float3 Lo = BRDF(L, V, N, material);

    float3 color = material.albedo * 0.3 + Lo;

    return float4(color, 1.0);

    //return float4(material.albedo.rgb, 1.0);
    //return float4(material.metallic.xxx, 1.0);
    //return float4(material.roughness.xxx, 1.0);
    //return float4(N * 0.5 + 0.5, 1.0).
    //return float4(V, 1.0);
    //return float4(Lo, 1.0);
}