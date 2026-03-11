import common;
import shadowCommon;

struct VSInput
{
    uint vertexID   : SV_VertexID;
    float3 Pos      : POSITION;
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
    //output.Pos = mul(float4(input.Pos, 1.0), frameData.depthMVP);
    float4 worldPos = mul(float4(input.Pos, 1.0), frameData.Model);
    output.Pos = mul(worldPos, frameData.cascadeViewProj[pc.cascadeID]);

    return output;
}

[shader("fragment")]
float4 fragmentMain()
{
	return float4(1.0, 0.1, 0.1, 1.0);
}