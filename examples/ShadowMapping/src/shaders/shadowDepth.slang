import bindings;
import shadowCommon;

struct DrawData
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
    uint pad;
    float4x4 transform;
};

//TODO: Reduce to just Pos
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
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, drawIndex);

    float4 localTransformed = mul(float4(input.Pos, 1.0), draw.transform);
    output.Pos = mul(localTransformed, frameData.depthMVP);

    return output;
}

[shader("fragment")]
float4 fragmentMain()
{
	return float4(1.0, 0.1, 0.1, 1.0);
}