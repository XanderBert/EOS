import pbrShared;
import common;

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

struct DrawData
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
    uint pad;
    float4x4 transform;
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float4   cameraPos;
};

struct PushConstants
{
    ConstBufferPointer<PerFrameData> PerFramePointer;
    uint64_t PerDrawPointer;
};

struct PSOutput
{
    float4 AlbedoMetallic  : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 WorldPosition   : SV_Target2;
};

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = pc.PerFramePointer.get();
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, (uint64_t)drawIndex);

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
PSOutput fragmentMain(VSOutput input)
{
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, (uint64_t)input.DrawIndex);

    MaterialTextureSet materialTextures;
    materialTextures.albedo = draw.albedo;
    materialTextures.normal = draw.normal;
    materialTextures.metallicRoughness = draw.metallicRoughness;

    Material material = SampleMaterial(materialTextures, 0, input.UV);
    float3 shadingNormal = SampleNormal(materialTextures, 0, input.UV, input.Normal, input.WorldPos, input.Tangent);

    PSOutput output;
    output.AlbedoMetallic = float4(material.albedo, material.metallic);
    output.NormalRoughness = float4(normalize(shadingNormal) * 0.5 + 0.5, material.roughness);
    output.WorldPosition = float4(input.WorldPos, 1.0);
    return output;
}