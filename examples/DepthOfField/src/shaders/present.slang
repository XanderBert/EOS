import common;

public struct PresentPC
{
    public uint sceneColorID;
    public uint samplerID;
    public uint pad0;
    public uint pad1;
};

[[vk::push_constant]] PresentPC pc;

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOutput o;
    o.UV  = uv;
    o.Pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    float2 uv = input.UV;
    uv.y = 1.0 - uv.y;
    return TextureBindless2D(pc.sceneColorID, pc.samplerID, uv);
}
