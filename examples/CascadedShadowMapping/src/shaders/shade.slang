import pbrShared;
import common;
import shadowCommon;

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
};


uint SelectCascade(float viewDepth, float4 cascadeSplits)
{
    if (viewDepth < cascadeSplits.x) { return 0; }
    if (viewDepth < cascadeSplits.y) { return 1; }
    if (viewDepth < cascadeSplits.z) { return 2; }
    return 3;
}

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
    pbrInput.lightPos = frameData.lightPos.xyz;

    Material material = SampleMaterial(materialTextures, 0, input.UV);
    float3 shadingNormal = SampleNormal(materialTextures, 0, input.UV, input.Normal, input.WorldPos, input.Tangent);
    PBRSurface pbrSurface = BuildPBRSurface(pbrInput, material, shadingNormal);
    float3 color = EvaluatePBR(pbrSurface);

    float3 N = pbrSurface.normal;
    float3 L = normalize(frameData.lightPos.xyz - input.WorldPos);


    float4 viewPos = mul(float4(input.WorldPos, 1.0), frameData.View);
    float viewDepth = -viewPos.z;
    uint cascadeIndex = SelectCascade(viewDepth, frameData.cascadeSplits);

    if(frameData.showCascades)
    {
        float4 color4 = float4(color, 1);
        if(cascadeIndex == 0) return color4 * float4(1, 0 ,0 ,1);
        if(cascadeIndex == 1) return color4 * float4(0, 1 ,0 ,1);
        if(cascadeIndex == 2) return color4 * float4(0, 0 ,1 ,1);
        if(cascadeIndex == 3) return color4 * float4(1, 0 ,1 ,1);
    }

    float4 shadowCoord = mul(float4(input.WorldPos, 1.0), frameData.cascadeViewProj[cascadeIndex]);
    float3 projCoords = shadowCoord.xyz / shadowCoord.w;

    //If the coord is outside the light frustum don't shadow it.
    if(projCoords.z <= 0.0 || projCoords.z >= 1.0) return float4(color, 1.0);
    
    // Only remap XY -> Vulkan depth already stays in [0, 1].
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // EOS uses a flipped Vulkan viewport, so shadow-map UV Y must be flipped.
    projCoords.y = 1.0 - projCoords.y;

    if (projCoords.x <= 0.0 || projCoords.x >= 1.0 || projCoords.y <= 0.0 || projCoords.y >= 1.0)
    {
        return float4(color, 1.0);
    }


    //TODO: Interpolate this scale as now, this will have a hard change while changing cascade
    float cascadeScale = 1.0 - float(cascadeIndex) * 0.15;
    float ndotl = saturate(dot(N, L));
    float bias = ndotl * 0.009 * cascadeScale;

    float currentDepth = projCoords.z;
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(4096.0, 4096.0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x,y) * texelSize;
            float pcfDepth = TextureBindless2DArray(frameData.shadowMapID, 1, float3(projCoords.xy + offset, cascadeIndex)).r;
            shadow += currentDepth - bias > pcfDepth ? 0.3 : 1.0;
        }
    }
    shadow /= 9.0;

    return float4(color * shadow, 1.0);
}