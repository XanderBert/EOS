[shader("vertex")]
void vertexMain(
    uint vertexID : SV_VertexID,
    float3 inPos : POSITION,
    float2 inUV  : TEXCOORD0,

    out float4 outPosition : SV_Position,
    out float2 outUV       : TEXCOORD0)
{
    outPosition = mul(float4(inPos, 1.0), perFrameData.MVP);
    outUV = inUV;
}

public struct PerFrameData
{
    float4x4 MVP;
    uint textureID;
};


[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(0, 0)]] Texture2D kTextures2D[];
[[vk::push_constant]] PerFrameData perFrameData;

float4 textureBindless2D(uint textureid, uint samplerid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].Sample(kSamplers[NonUniformResourceIndex(samplerid)], uv);
}

[shader("fragment")]
void fragmentMain(
    float2 inUV : TEXCOORD0,
    out float4 outFragColor : SV_Target)
{
    outFragColor = textureBindless2D(perFrameData.textureID, 0, inUV);
}
