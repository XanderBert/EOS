import common;

struct ImGuiPushConstants
{
    float4   LRTB;
    uint64_t vertexBuffer;
    uint     textureId;
    uint     samplerId;
    uint     textureLayer;
    uint     textureView;
};

struct ImGuiVertex
{
    float x, y;
    float u, v;
    uint  rgba;
};

struct VSOutput
{
    float4 Pos   : SV_Position;
    float4 Color : TEXCOORD0;
    float2 UV    : TEXCOORD1;
};

[[vk::push_constant]] ImGuiPushConstants pc;

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    ImGuiVertex v = LoadBufferArray<ImGuiVertex>(pc.vertexBuffer, vertexID);

    float L = pc.LRTB.x;
    float R = pc.LRTB.y;
    float T = pc.LRTB.z;
    float B = pc.LRTB.w;

    float4x4 proj = float4x4(
        2.0 / (R - L),          0.0,           0.0, 0.0,
        0.0,           2.0 / (T - B),           0.0, 0.0,
        0.0,                    0.0,            -1.0, 0.0,
        (R + L) / (L - R), (T + B) / (B - T),   0.0, 1.0
    );

    VSOutput output;
    output.Pos   = mul(float4(v.x, v.y, 0.0, 1.0), proj);
    output.Color = unpackUnorm4x8ToFloat(v.rgba);
    output.UV    = float2(v.u, v.v);
    return output;
}

[[vk::constant_id(0)]] const bool kNonLinearColorSpace = false;

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    float4 sampledColor;
    if (pc.textureView == 1)
    {
        sampledColor = TextureBindless2DArray(pc.textureId, pc.samplerId, float3(input.UV, float(pc.textureLayer)));
    }
    else
    {
        sampledColor = TextureBindless2D(pc.textureId, pc.samplerId, input.UV);
    }

    float4 c = input.Color * sampledColor;
    return kNonLinearColorSpace ? float4(pow(c.rgb, float3(2.2)), c.a) : c;
}