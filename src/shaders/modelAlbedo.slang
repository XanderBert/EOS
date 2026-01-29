[shader("vertex")]
void vertexMain(
    uint vertexID : SV_VertexID,
    float3 inPos : POSITION,
    out float4 outPosition : SV_Position,
    out float3 outColor : COLOR0)
{
    outPosition = mul(float4(inPos, 1.0), perFrameData.MVP);
    outColor = inPos.xzy;
}

public struct PerFrameData
{
    float4x4 MVP;
    uint textureID;
};

[[vk::binding(0, 0)]] Texture2D kTextures2D[];
[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::push_constant]] PerFrameData perFrameData;

// Fixed function declaration - added return type
float4 textureBindless2D(uint textureid, uint samplerid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].Sample(
        kSamplers[NonUniformResourceIndex(samplerid)],
        uv
    );
}

[shader("fragment")]
void fragmentMain(
    float3 inColor : COLOR0,
    out float4 outFragColor : SV_Target)
{
    float2 uv = inColor.xy * 0.5 + 0.5;
    outFragColor = textureBindless2D(perFrameData.textureID, 0, uv);
}