import common;

public struct EdgeDetectPC
{
    public uint   sceneColorID;
    public uint   sceneNormalID;
    public uint   samplerID;
    public float  threshold;
    public uint   showEdgesOnly;
    public float  texelW;
    public float  texelH;
    public uint   pad0;
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
    float2 uv = float2((id << 1) & 2, id & 2);

    VSOutput o;
    o.UV  = uv;
    o.Pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float3 DecodeNormal(float3 packedNormal)
{
    return normalize(packedNormal * 2.0 - 1.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    float2 uv = input.UV;
    uv.y = 1 - uv.y;

    float2 ts = float2(pc.texelW, pc.texelH);
    float3 tl = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2(-1.0,  1.0) * ts).rgb);
    float3 tm = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2( 0.0,  1.0) * ts).rgb);
    float3 tr = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2( 1.0,  1.0) * ts).rgb);
    float3 ml = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2(-1.0,  0.0) * ts).rgb);
    float3 mr = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2( 1.0,  0.0) * ts).rgb);
    float3 bl = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2(-1.0, -1.0) * ts).rgb);
    float3 bm = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2( 0.0, -1.0) * ts).rgb);
    float3 br = DecodeNormal(TextureBindless2D(pc.sceneNormalID, pc.samplerID, uv + float2( 1.0, -1.0) * ts).rgb);

    float3 gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    float3 gy =  tl + 2.0 * tm + tr - bl - 2.0 * bm - br;
    float edge = sqrt(dot(gx, gx) + dot(gy, gy));

    if (pc.showEdgesOnly != 0)
    {
        float e = step(pc.threshold, edge);
        return float4(e, e, e, 1.0);
    }

    uv.y = 1 - uv.y;
    float4 litScene = TextureBindless2D(pc.sceneColorID, pc.samplerID, uv);
    float edgeMask = 1.0 - saturate((edge - pc.threshold) * 10.0);
    return float4(litScene.rgb * edgeMask, 1.0);
}
