import eos.material;
import eos.lighting;

struct VSInput
{
    uint vertexID   : SV_VertexID;
    float3 Pos      : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    float4 Tangent  : TANGENT;
};

struct VSOutput
{
    float4 Pos      : SV_Position;
    float3 WorldPos : TEXCOORD1;
    float3 Normal   : TEXCOORD2;
    float4 Tangent  : TANGENT;
    float2 UV       : TEXCOORD0;
    uint   DrawIndex : TEXCOORD3;
};

struct DrawData
{
    float4x4 transform;
    StandardMaterialData material;
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float4   cameraPos;
};

struct PushConstants
{
    PerFrameData* perFrame;
    DrawData* draws;
};

[[vk::push_constant]] PushConstants pc;

static const float3 kSunDirection = normalize(float3(-1.0, -1.0, -1.0));
static const float3 kSunRadiance = float3(3.0);
static const float3 kAmbientRadiance = float3(0.3);

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    const PerFrameData frameData = *pc.perFrame;
    const float4x4 transform = pc.draws[drawIndex].transform;

    const float4 localTransformed = mul(float4(input.Pos, 1.0), transform);
    const float3x3 normalMatrix   = float3x3(mul(transform, frameData.Model));

    VSOutput output;
    output.Pos          = mul(localTransformed, frameData.MVP);
    output.WorldPos     = mul(localTransformed, frameData.Model).xyz;
    output.Normal       = mul(input.Normal, normalMatrix);
    output.Tangent      = float4(mul(input.Tangent.xyz, normalMatrix), input.Tangent.w);
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    const PerFrameData frameData = *pc.perFrame;
    const SurfaceData surface = SurfaceData.FromVertex(input.WorldPos, input.Normal, input.Tangent, input.UV, frameData.cameraPos.xyz);
    const StandardMaterial material = StandardMaterial(pc.draws[input.DrawIndex].material);
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    DirectionalLight sun;
    sun.direction = kSunDirection;
    sun.radiance = kSunRadiance;

    const float3 color = EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun)
                       + EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, kAmbientRadiance, point.occlusion)
                       + point.emission;
    return float4(color, 1.0);
}
