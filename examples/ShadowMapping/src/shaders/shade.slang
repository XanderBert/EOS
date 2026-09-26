import bindings;
import shading;
import shadowCommon;
import math;

struct MaterialHandles
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
}

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
    float3 Tangent  : TANGENT;
    float2 UV       : TEXCOORD0;
    uint   DrawIndex : TEXCOORD3;
    float4 ShadowCoord;
};

struct DrawData
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
    uint pad;
    float4x4 transform;
};


// Must match the shadow depth texture created in main.cpp.
static const float kShadowMapSize = 4096.0;

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, drawIndex);

    float4 localTransformed = mul(float4(input.Pos, 1.0), draw.transform);
    float3x3 normalMatrix   = float3x3(mul(draw.transform, frameData.Model));

    VSOutput output;
    output.Pos          = mul(localTransformed, frameData.MVP);
    output.WorldPos     = mul(localTransformed, frameData.Model).xyz;
    output.Normal       = normalize(mul(input.Normal, normalMatrix));
    output.Tangent      = mul(input.Tangent.xyz, normalMatrix);
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;
    output.ShadowCoord  = mul(localTransformed, frameData.depthMVP);

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    // Sample textures
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, input.DrawIndex);


    MaterialTextureSet materialTextures;
    materialTextures.albedo = draw.albedo;
    materialTextures.normal = draw.normal;
    materialTextures.metallicRoughness = draw.metallicRoughness;

    PBRShadingInput pbrInput;
    pbrInput.worldPos = input.WorldPos;
    pbrInput.geometricNormal = input.Normal;
    pbrInput.tangent = input.Tangent;
    pbrInput.uv = input.UV;
    pbrInput.cameraPos = frameData.cameraPos;

    // The shadow map uses an orthographic projection, so the light is directional:
    // shade with the same direction the shadow map is rendered from, not with lightPos - worldPos.
    float3 L = SafeNormalize(-frameData.lightDir.xyz, float3(0.0, 1.0, 0.0));
    pbrInput.lightPos = input.WorldPos + L * 100000.0;

    Material material = SampleMaterial(materialTextures, 0, input.UV);
    float3 shadingNormal = SampleNormal(materialTextures, 0, input.UV, input.Normal, input.WorldPos, input.Tangent);
    PBRSurface pbrSurface = BuildPBRSurface(pbrInput, material, shadingNormal);
    pbrSurface.lightDir = L;
    float3 color = EvaluatePBR(pbrSurface);

    float3 projCoords = input.ShadowCoord.xyz / input.ShadowCoord.w;

    // Only remap XY -> Vulkan depth already stays in [0, 1].
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // EOS uses a flipped Vulkan viewport, so shadow-map UV Y must be flipped.
    projCoords.y = 1.0 - projCoords.y;

    // If the coord is outside the light frustum don't shadow it (the border colour would read as occluded).
    if (projCoords.z > 1.0 || any(projCoords.xy < 0.0) || any(projCoords.xy > 1.0)) return float4(color, 1.0);

    // Slope bias from the geometric normal; normal-map detail shouldn't move the depth comparison.
    float ndotl = saturate(dot(normalize(input.Normal), L));
    float bias = max(0.0005 * (1.0 - ndotl), 0.00005);
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(kShadowMapSize, kShadowMapSize);

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = TextureBindless2D(frameData.shadowMapID, 1, projCoords.xy + float2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 0.3 : 1.0;
        }
    }
    shadow /= 9.0;

    return float4(color * shadow, 1.0);
}