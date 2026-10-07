// The G-buffer: draws the scene and writes, per pixel, the parameters of its material's BSDF and where it is. The
// lighting pass rebuilds a StandardBSDF from them.
import eos.core;
import eos.math;
import eos.material;
import eos.pass;
import eos.scene;
import eos.view;

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
struct GBuffer
{
    [Input] Scene* scene;
    [Input] View* view;
    [Output] [Format("Z_F32")] [DepthTest(CompareOp.Less)] DepthTarget depth;
};

[[vk::push_constant]] GBuffer pass;

struct VSOutput
{
    float4 position                 : SV_Position;
    float3 worldPosition            : TEXCOORD1;
    float3 normal                   : TEXCOORD2;
    float4 tangent                  : TANGENT;
    float2 uv                       : TEXCOORD0;
    nointerpolation uint instance   : TEXCOORD3;
};

struct Targets
{
    [Output] [Format("RGBA_UN8")] [Clear(0.36, 0.4, 1.0, 1.0)] float4 albedo   : SV_Target0;   // base color, metallic
    [Output] [Format("RGBA_UN8")] [Clear(0.5, 0.5, 1.0, 1.0)]  float4 normal   : SV_Target1;   // packed normal, roughness
    [Output] [Format("RGBA_F16")] [Clear(0.0, 0.0, 0.0, 1.0)]  float4 worldPos : SV_Target2;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    const MeshVertex vertex = pass.scene.vertices[vertexID];
    const float4x4 transform = pass.scene.instances[instanceID].transform;
    const float4 worldPosition = mul(float4(vertex.position, 1.0), transform);
    const float3x3 normalMatrix = float3x3(transform);

    VSOutput output;
    output.position      = mul(worldPosition, pass.view.viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal        = mul(vertex.normal, normalMatrix);
    output.tangent       = float4(mul(vertex.tangent.xyz, normalMatrix), vertex.tangent.w);
    output.uv            = vertex.uv;
    output.instance      = instanceID;
    return output;
}

[shader("fragment")]
Targets fragmentMain(VSOutput input)
{
    const SurfaceData surface = SurfaceData.FromVertex(input.worldPosition, input.normal, input.tangent, input.uv, pass.view.position);
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    let point = material.Evaluate(surface, ImplicitLodSampler());
    if (!material.PassesAlphaTest(point.opacity)) discard;

    Targets targets;
    targets.albedo   = float4(point.bsdf.baseColor, point.bsdf.metallic);
    targets.normal   = float4(PackNormalUnorm(point.frame.N), point.bsdf.perceptualRoughness);
    targets.worldPos = float4(input.worldPosition, 1.0);
    return targets;
}
