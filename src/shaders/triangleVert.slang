public struct PerFrameData
{
    float4x4 MVP;
};

[[vk::push_constant]] PerFrameData perFrameData;


[shader("vertex")]
void main( uint vertexID : SV_VertexID, float3 inPos : POSITION, out float4 outPosition : SV_Position, out float3 outColor : COLOR0)
{
    outPosition = mul(float4(inPos, 1.0), perFrameData.MVP);
    outColor = inPos.xzy;
}