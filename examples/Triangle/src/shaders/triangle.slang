// A triangle in three colors: the smallest render graph pass written in Slang. It has a vertex shader of its own, so
// the pass draws its 3 vertices.
import eos.pass;

[Pass]
struct Triangle
{
    [Range(0.1, 2.0)] float scale = 1.0;
};

[[vk::push_constant]] Triangle pass;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

static const float2 positions[3] =
{
    float2(-0.5f, -0.5f),
    float2( 0.5f, -0.5f),
    float2(0.0f, 0.5f),
};

static const float3 colors[3] =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0),
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(positions[vertexID] * pass.scale, 0.0, 1.0);
    output.color    = colors[vertexID];
    return output;
}

struct Targets
{
    [Output] [Clear(0.36, 0.4, 1.0, 0.28)] float4 color : SV_Target0;
};

[shader("fragment")]
Targets fragmentMain(VertexOutput input)
{
    Targets targets;
    targets.color = float4(input.color, 1.0);
    return targets;
}
