import eos.material;
import shadowCommon;

struct VSInput
{
    uint vertexID : SV_VertexID;
    float3 Pos    : POSITION;
    float2 UV     : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos       : SV_Position;
    float2 UV        : TEXCOORD0;
    uint   DrawIndex : TEXCOORD1;
};

[[vk::push_constant]] FramePointers pc;

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    const float4 localTransformed = mul(float4(input.Pos, 1.0), pc.draws[drawIndex].transform);

    VSOutput output;
    output.Pos = mul(localTransformed, pc.perFrame->mvp);
    output.UV = input.UV;
    output.DrawIndex = drawIndex;
    return output;
}

// Opaque draws: no discard, so the GPU keeps its early depth test and fast depth-only path.
[shader("fragment")]
void fragmentOpaque()
{
}

// Alpha-tested draws are cut out here as well: depth written for their holes would hide what is behind them.
[shader("fragment")]
void fragmentMasked(VSOutput input)
{
    const StandardMaterial material = StandardMaterial(pc.draws[input.DrawIndex].material);
    if (!material.PassesAlphaTest(material.EvaluateOpacity(SurfaceData.FromUV(input.UV), ImplicitLodSampler()))) discard;
}
