import eos.material;
import shadowCommon;

struct VSInput
{
    uint vertexID   : SV_VertexID;
    float3 Pos      : POSITION;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 WorldPos  : TEXCOORD0;
    float2 UV        : TEXCOORD1;
    uint   DrawIndex : TEXCOORD2;
};

struct GSOutput
{
    float4 Pos       : SV_Position;
    uint   Layer     : SV_RenderTargetArrayIndex;
    float2 UV        : TEXCOORD1;
    uint   DrawIndex : TEXCOORD2;
};

[[vk::push_constant]] FramePointers pc;

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    const float4 localTransformed = mul(float4(input.Pos, 1.0), pc.draws[drawIndex].transform);

    VSOutput output;
    output.WorldPos = mul(localTransformed, pc.perFrame->model);
    output.UV = input.UV;
    output.DrawIndex = drawIndex;
    return output;
}

[maxvertexcount(12)]
[shader("geometry")]
void geometryMain(triangle VSOutput input[3], inout TriangleStream<GSOutput> triangleStream)
{
    const PerFrameData frameData = *pc.perFrame;

    [unroll]
    for (uint cascadeID = 0; cascadeID < 4; ++cascadeID)
    {
        [unroll]
        for (uint i = 0; i < 3; ++i)
        {
            GSOutput output;
            output.Pos = mul(input[i].WorldPos, frameData.cascadeViewProj[cascadeID]);
            output.Layer = cascadeID;
            output.UV = input[i].UV;
            output.DrawIndex = input[i].DrawIndex;
            triangleStream.Append(output);
        }

        triangleStream.RestartStrip();
    }
}

// Opaque draws: no discard, so the GPU keeps its early depth test and fast depth-only path.
[shader("fragment")]
void fragmentOpaque()
{
}

// Alpha-tested draws only cast shadows where they are not cut away.
[shader("fragment")]
void fragmentMasked(GSOutput input)
{
    const StandardMaterial material = StandardMaterial(pc.draws[input.DrawIndex].material);
    if (!material.PassesAlphaTest(material.EvaluateOpacity(SurfaceData.FromUV(input.UV), ImplicitLodSampler()))) discard;
}
