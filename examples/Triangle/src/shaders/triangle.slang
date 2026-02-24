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
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color    = colors[vertexID];
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}