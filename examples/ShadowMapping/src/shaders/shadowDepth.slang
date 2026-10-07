// The shadow map: the depth of the scene as the light sees it, through the View of the light (the lightView node in
// main.cpp). Front faces are culled, so the depth the shade pass compares against is that of back faces.
import eos.core;
import eos.material;
import eos.pass;
import eos.scene;
import eos.view;

[Pass]
[DrawScene]
[Cull(CullMode.Front)]
struct ShadowDepth
{
    [Input] Scene* scene;
    [Input] View* view;
    [Output] [Format("Z_F32")] [Size(4096, 4096)] DepthTarget shadowMap;
};

[[vk::push_constant]] ShadowDepth pass;

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

// Alpha-tested instances only cast shadows where they are not cut away.
[shader("fragment")]
[Materials(AlphaMode.Mask)]
void fragmentMasked(VSOutput input)
{
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    if (!material.PassesAlphaTest(material.EvaluateOpacity(SurfaceData.FromUV(input.uv), ImplicitLodSampler()))) discard;
}
