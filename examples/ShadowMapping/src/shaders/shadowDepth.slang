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

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
float4 vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    //return mul(float4(input.Pos, 1.0), frameData.depthMVP);
    return mul(mul(float4(input.Pos, 1.0), frameData.Model), frameData.depthMVP);
    //return mul(frameData.depthMVP, mul(frameData.Model, float4(input.Pos, 1.0)));
}

[shader("fragment")]
float4 fragmentMain()
{
	return float4(1.0, 0.0, 0.0, 1.0);
}