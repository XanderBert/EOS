// Draws the model, turned by an object transform on top of its instances' own, lit by one directional light and a
// constant ambient term.
import eos.core;
import eos.material;
import eos.lighting;
import eos.pass;
import eos.scene;
import eos.view;

// What the turntable node (main.cpp) uploads every frame.
[CppExport]
public struct ObjectTransform
{
    public float4x4 model;      // applied after each instance's transform
};

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
struct Model
{
    [Input] Scene* scene;
    [Input] View* view;
    [Input] ObjectTransform* object;
    [Output] [Format("Z_F32")] DepthTarget depth;

    [Range(-1.0, 1.0)] float3 lightDirection;       // the direction the light travels
    [Range(0.0, 10.0)] float  lightIntensity = 3.0;
    [Range(0.0, 1.0)]  float3 lightColor;
    [Range(0.0, 1.0)]  float  ambient = 0.3;        // radiance of the uniform environment that stands in for image-based lighting
};

[[vk::push_constant]] Model pass;

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
    [Output] [Clear(0.36, 0.4, 1.0, 0.28)] float4 color : SV_Target0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    const MeshVertex vertex = pass.scene.vertices[vertexID];
    const float4x4 transform = mul(pass.scene.instances[instanceID].transform, pass.object.model);
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

    DirectionalLight sun;
    sun.direction = normalize(pass.lightDirection);
    sun.radiance = pass.lightColor * pass.lightIntensity;

    Targets targets;
    targets.color = float4(EvaluateDirectionalLight(point.bsdf, point.frame, surface.viewDirection, sun)
                         + EvaluateConstantAmbient(point.bsdf, point.frame, surface.viewDirection, pass.ambient.xxx, point.occlusion)
                         + point.emission, 1.0);
    return targets;
}
