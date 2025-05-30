// Slang version

[shader("vertex")]
void main(
    uint vertexID : SV_VertexID,
    out float4 outPosition : SV_Position,
    out float3 outColor : COLOR0
)
{
    static const float2 pos[3] = {
        float2(-0.6, -0.4),
        float2( 0.6, -0.4),
        float2( 0.0,  0.6)
    };

    static const float3 col[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };

    outPosition = float4(pos[vertexID], 0.0, 1.0);
    outColor = col[vertexID];
}
