import bindings;
import shadowCommon;

struct VSInput
{
    uint vertexID : SV_VertexID;
    float3 Pos : POSITION;
};

struct VSOutput
{
    float4 Pos : SV_Position;
};

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, drawIndex);

    float4 localTransformed = mul(float4(input.Pos, 1.0), draw.transform);

    VSOutput output;
    output.Pos = mul(localTransformed, frameData.MVP);
    return output;
}

[shader("fragment")]
void fragmentMain()
{
}
