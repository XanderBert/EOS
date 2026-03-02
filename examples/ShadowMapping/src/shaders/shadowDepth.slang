import common;
import shadowCommon;


//TODO: Reduce to just Pos
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
}

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    VSOutput output;
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    output.Pos = mul(float4(input.Pos, 1.0), frameData.depthMVP);

    return output;
}

[shader("fragment")]
float4 fragmentMain()
{
	return float4(1.0, 0.1, 0.1, 1.0);
}