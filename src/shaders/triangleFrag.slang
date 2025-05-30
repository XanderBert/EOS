[shader("fragment")]
void main(
    float3 inColor : COLOR0,
    out float4 outFragColor : SV_Target
)
{
    outFragColor = float4(inColor, 1.0);
}
