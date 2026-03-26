import common;
import shadowCommon;

struct VSInput
{
    uint vertexID   : SV_VertexID;
    float3 Pos      : POSITION;
};

struct VSOutput
{
    float4 WorldPos : TEXCOORD0;
};

struct GSOutput
{
    float4 Pos   : SV_Position;
    uint Layer   : SV_RenderTargetArrayIndex;
};

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    VSOutput output;
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, drawIndex);

    float4 localTransformed = mul(float4(input.Pos, 1.0), draw.transform);
    float4 worldPos = mul(localTransformed, frameData.Model);
    output.WorldPos = worldPos;

    return output;
}

[maxvertexcount(12)]
[shader("geometry")]
void geometryMain(triangle VSOutput input[3], inout TriangleStream<GSOutput> triangleStream)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    [unroll]
    for (uint cascadeID = 0; cascadeID < 4; ++cascadeID)
    {
        [unroll]
        for (uint i = 0; i < 3; ++i)
        {
            GSOutput output;
            output.Pos = mul(input[i].WorldPos, frameData.cascadeViewProj[cascadeID]);
            output.Layer = cascadeID;
            triangleStream.Append(output);
        }

        triangleStream.RestartStrip();
    }
}

[shader("fragment")]
float4 fragmentMain()
{
	return float4(1.0, 0.1, 0.1, 1.0);
}