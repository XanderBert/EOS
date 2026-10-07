// The shadow map: a layer per cascade, all rendered in one pass. The geometry shader sends every triangle to each
// cascade's layer, projected with that cascade's matrix. Depth is clamped rather than clipped, so casters between the
// light and a cascade's near plane still shadow it.
import eos.core;
import eos.material;
import eos.pass;
import eos.scene;
import shadowCommon;

[Pass]
[DrawScene]
[Cull(CullMode.Back)]
[DepthClamp]
struct ShadowDepth
{
    [Input] Scene* scene;
    [Input] CascadeData* cascades;
    [Output] [Format("Z_F32")] [Size(kShadowMapSize, kShadowMapSize)] [Layers(kCascadeCount)] DepthTarget shadowMap;
};

[[vk::push_constant]] ShadowDepth pass;

struct VSOutput
{
    float4 worldPosition            : TEXCOORD0;
    float2 uv                       : TEXCOORD1;
    nointerpolation uint instance   : TEXCOORD2;
};

struct GSOutput
{
    float4 position                 : SV_Position;
    uint layer                      : SV_RenderTargetArrayIndex;
    float2 uv                       : TEXCOORD1;
    nointerpolation uint instance   : TEXCOORD2;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    const MeshVertex vertex = pass.scene.vertices[vertexID];

    VSOutput output;
    output.worldPosition = mul(float4(vertex.position, 1.0), pass.scene.instances[instanceID].transform);
    output.uv = vertex.uv;
    output.instance = instanceID;
    return output;
}

[maxvertexcount(12)]
[shader("geometry")]
void geometryMain(triangle VSOutput input[3], inout TriangleStream<GSOutput> triangleStream)
{
    const CascadeData cascades = *pass.cascades;

    [unroll]
    for (uint cascade = 0; cascade < kCascadeCount; ++cascade)
    {
        [unroll]
        for (uint i = 0; i < 3; ++i)
        {
            GSOutput output;
            output.position = mul(input[i].worldPosition, cascades.viewProjection[cascade]);
            output.layer = cascade;
            output.uv = input[i].uv;
            output.instance = input[i].instance;
            triangleStream.Append(output);
        }

        triangleStream.RestartStrip();
    }
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
void fragmentMasked(GSOutput input)
{
    const StandardMaterial material = StandardMaterial(pass.scene.GetMaterial(input.instance));
    if (!material.PassesAlphaTest(material.EvaluateOpacity(SurfaceData.FromUV(input.uv), ImplicitLodSampler()))) discard;
}
