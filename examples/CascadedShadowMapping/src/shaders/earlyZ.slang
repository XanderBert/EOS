// The depth of the scene before it is shaded: the depth reduction fits the cascades to it, and the shade pass shades
// exactly the surfaces it kept (depth test Equal), once per pixel.
import eos.core;
import eos.material;
import eos.pass;
import eos.scene;
import eos.view;

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
struct EarlyZ
{
    [Input] Scene* scene;
    [Input] View* view;
    [Output] [Format("Z_F32")] DepthTarget depth;
};

[[vk::push_constant]] EarlyZ pass;

struct VSOutput
{
    float4 position                 : SV_Position;
    float2 uv                       : TEXCOORD0;
    nointerpolation uint instance   : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    const MeshVertex vertex = pass.scene.vertices[vertexID];
    const float4 worldPosition = mul(float4(vertex.position, 1.0), pass.scene.instances[instanceID].transform);

    VSOutput output;
    output.position = mul(worldPosition, pass.view.viewProjection);
    output.uv = vertex.uv;
    output.instance = instanceID;
    return output;
}

// Opaque instances: no discard, so the GPU keeps its early depth test and fast depth-only path.
[shader("fragment")]
[Materials(AlphaMode.Opaque)]
[Materials(AlphaMode.Blend)]
void fragmentOpaque()
{
}

// Alpha-tested instances are cut out here as well: depth written for their holes would hide what is behind them.
[shader("fragment")]
[Materials(AlphaMode.Mask)]
void fragmentMasked(VSOutput input)
{
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    if (!material.PassesAlphaTest(material.EvaluateOpacity(SurfaceData.FromUV(input.uv), ImplicitLodSampler()))) discard;
}
