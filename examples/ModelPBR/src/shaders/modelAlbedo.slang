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
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float3 cameraPos;
    StandardMaterialData material;
};

struct PushConstants
{
    PerFrameData* perFrame;
};

[[vk::push_constant]] PushConstants pc;

static const float3 kSunDirection = normalize(float3(-1.0, -1.0, -1.0));
static const float3 kSunRadiance = float3(3.0);
static const float3 kAmbientRadiance = float3(0.3);

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    const PerFrameData frameData = *pc.perFrame;
    const float3x3 normalMatrix = float3x3(frameData.Model);

    VSOutput output;
    output.Pos          = mul(float4(input.Pos, 1.0), frameData.MVP);
    output.WorldPos     = mul(float4(input.Pos, 1.0), frameData.Model).xyz;
    output.Normal       = mul(input.Normal, normalMatrix);
    output.Tangent      = float4(mul(input.Tangent.xyz, normalMatrix), input.Tangent.w);
    output.UV           = input.UV;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    const PerFrameData frameData = *pc.perFrame;
    const SurfaceData surface = SurfaceData.FromVertex(input.WorldPos, input.Normal, input.Tangent, input.UV, frameData.cameraPos);
    const StandardMaterial material = StandardMaterial(frameData.material);
    let point = material.Evaluate(surface, ImplicitLodSampler());

    DirectionalLight sun;
    sun.direction = kSunDirection;
    sun.radiance = kSunRadiance;

    const float3 color = EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun)
                       + EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, kAmbientRadiance, point.occlusion)
                       + point.emission;
    return float4(color, 1.0);
}
