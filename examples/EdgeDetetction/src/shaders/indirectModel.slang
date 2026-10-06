import eos.core;
import eos.math;
import eos.material;

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

[CppExport]
public struct DrawData
{
    public float4x4 transform;
    public StandardMaterialData material;
};

[CppExport]
public struct PerFrameData
{
    public float4x4 model;
    public float4x4 mvp;
    public float4   cameraPos;
};

// Push constants of the G-buffer pass.
[CppExport]
public struct FramePointers
{
    public PerFrameData* perFrame;
    public DrawData* draws;
};

struct PSOutput
{
    float4 AlbedoMetallic  : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 WorldPosition   : SV_Target2;
};

[[vk::push_constant]] FramePointers pc;

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    const PerFrameData frameData = *pc.perFrame;
    const float4x4 transform = pc.draws[drawIndex].transform;

    const float4 localTransformed = mul(float4(input.Pos, 1.0), transform);
    const float3x3 normalMatrix   = float3x3(mul(transform, frameData.model));

    VSOutput output;
    output.Pos          = mul(localTransformed, frameData.mvp);
    output.WorldPos     = mul(localTransformed, frameData.model).xyz;
    output.Normal       = mul(input.Normal, normalMatrix);
    output.Tangent      = float4(mul(input.Tangent.xyz, normalMatrix), input.Tangent.w);
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;
    return output;
}

// Writes the parameters of the material's BSDF; the lighting pass rebuilds a StandardBSDF from them.
[shader("fragment")]
PSOutput fragmentMain(VSOutput input)
{
    const SurfaceData surface = SurfaceData.FromVertex(input.WorldPos, input.Normal, input.Tangent, input.UV, pc.perFrame->cameraPos.xyz);
    const StandardMaterial material = StandardMaterial(pc.draws[input.DrawIndex].material);
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    PSOutput output;
    output.AlbedoMetallic = float4(point.bsdf.baseColor, point.bsdf.metallic);
    output.NormalRoughness = float4(PackNormalUnorm(point.frame.N), point.bsdf.perceptualRoughness);
    output.WorldPosition = float4(input.WorldPos, 1.0);
    return output;
}
