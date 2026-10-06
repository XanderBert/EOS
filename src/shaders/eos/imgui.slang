// Dear ImGui draw lists. Vertices are read from a buffer through a pointer; the texture is bindless.
module "eos/imgui";

import eos.bindless;
import eos.color;

struct ImGuiVertex
{
    float2 position;
    float2 uv;
    uint   rgba;
};

static const uint kTextureView2D      = 0;
static const uint kTextureView2DArray = 1;

struct ImGuiPushConstants
{
    float4                          LRTB;
    ImGuiVertex*                    vertices;
    DescriptorHandle<Texture2D>     texture;        // a Texture2DArray when textureView is kTextureView2DArray
    DescriptorHandle<SamplerState>  samplerState;
    uint                            textureLayer;
    uint                            textureView;
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
    const ImGuiVertex v = pc.vertices[vertexID];

    const float L = pc.LRTB.x;
    const float R = pc.LRTB.y;
    const float T = pc.LRTB.z;
    const float B = pc.LRTB.w;

    const float4x4 proj = float4x4(
        2.0 / (R - L),          0.0,           0.0, 0.0,
        0.0,           2.0 / (T - B),           0.0, 0.0,
        0.0,                    0.0,            -1.0, 0.0,
        (R + L) / (L - R), (T + B) / (B - T),   0.0, 1.0
    );

    VSOutput output;
    output.Pos   = mul(float4(v.position, 0.0, 1.0), proj);
    output.Color = unpackUnorm4x8ToFloat(v.rgba);
    output.UV    = v.uv;
    return output;
}

// ImGui colors are sRGB-encoded. On a swapchain with an sRGB format the hardware encodes on write, so they are
// decoded to linear first.
[[vk::constant_id(0)]] const bool kNonLinearColorSpace = false;

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float4 sampledColor;
    if (pc.textureView == kTextureView2DArray)
    {
        const DescriptorHandle<Texture2DArray> textureArray = DescriptorHandle<Texture2DArray>((uint2)pc.texture);
        sampledColor = textureArray.Sample(pc.samplerState, float3(input.UV, float(pc.textureLayer)));
    }
    else
    {
        sampledColor = pc.texture.Sample(pc.samplerState, input.UV);
    }

    const float4 color = input.Color * sampledColor;
    return kNonLinearColorSpace ? float4(SrgbToLinear(color.rgb), color.a) : color;
}
