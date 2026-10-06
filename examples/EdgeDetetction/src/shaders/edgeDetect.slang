import eos.math;
import eos.bindless;
import eos.fullscreen;

public struct EdgeDetectPC
{
    public DescriptorHandle<Texture2D>    sceneColor;
    public DescriptorHandle<Texture2D>    sceneNormal;
    public DescriptorHandle<SamplerState> samplerState;
    public float  threshold;
    public uint   showEdgesOnly;
    public float  texelW;
    public float  texelH;
};

[[vk::push_constant]] EdgeDetectPC pc;

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

float3 SampleNormal(float2 uv)
{
    return UnpackNormalUnorm(pc.sceneNormal.Sample(pc.samplerState, uv).rgb);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.UV;
    uv.y = 1 - uv.y;

    const float2 ts = float2(pc.texelW, pc.texelH);
    const float3 tl = SampleNormal(uv + float2(-1.0,  1.0) * ts);
    const float3 tm = SampleNormal(uv + float2( 0.0,  1.0) * ts);
    const float3 tr = SampleNormal(uv + float2( 1.0,  1.0) * ts);
    const float3 ml = SampleNormal(uv + float2(-1.0,  0.0) * ts);
    const float3 mr = SampleNormal(uv + float2( 1.0,  0.0) * ts);
    const float3 bl = SampleNormal(uv + float2(-1.0, -1.0) * ts);
    const float3 bm = SampleNormal(uv + float2( 0.0, -1.0) * ts);
    const float3 br = SampleNormal(uv + float2( 1.0, -1.0) * ts);

    const float3 gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    const float3 gy =  tl + 2.0 * tm + tr - bl - 2.0 * bm - br;
    const float edge = sqrt(dot(gx, gx) + dot(gy, gy));

    if (pc.showEdgesOnly != 0)
    {
        const float e = step(pc.threshold, edge);
        return float4(e, e, e, 1.0);
    }

    uv.y = 1 - uv.y;
    const float4 litScene = pc.sceneColor.Sample(pc.samplerState, uv);
    const float edgeMask = 1.0 - saturate((edge - pc.threshold) * 10.0);
    return float4(litScene.rgb * edgeMask, 1.0);
}
