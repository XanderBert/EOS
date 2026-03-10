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
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float3 cameraPos;
    MaterialTextureSet material;
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
    output.Tangent      = mul(input.Tangent.xyz, float3x3(frameData.Model));
    output.UV           = input.UV;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    PBRShadingInput pbrInput;
    pbrInput.worldPos = input.WorldPos;
    pbrInput.geometricNormal = input.Normal;
    pbrInput.tangent = input.Tangent;
    pbrInput.uv = input.UV;
    pbrInput.cameraPos = frameData.cameraPos;
    pbrInput.lightPos = float3(1.0, 1.0, 1.0);

    float3 color = ShadePBR(pbrInput, frameData.material);
    return float4(color, 1.0);
}