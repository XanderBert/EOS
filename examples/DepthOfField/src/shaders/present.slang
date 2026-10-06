import eos.core;
import eos.fullscreen;

[CppExport]
public struct PresentPC
{
    public DescriptorHandle<Texture2D>    sceneColor;
    public DescriptorHandle<SamplerState> samplerState;
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
    VSOutput o;
    FullscreenTriangle(id, o.UV, o.Pos);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.UV;
    uv.y = 1.0 - uv.y;
    return pc.sceneColor.Sample(pc.samplerState, uv);
}
