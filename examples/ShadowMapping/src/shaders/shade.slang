import pbr;
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
    float3 ViewVec;
    float3 LightVec;
    float4 ShadowCoord;
};

struct DrawData
{
    uint albedo;
    uint normal;
    uint metallicRoughness;
    uint pad;
};


[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input, uint drawIndex : SV_DrawIndex)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    VSOutput output;
    output.Pos          = mul(float4(input.Pos, 1.0), frameData.MVP);
    output.WorldPos     = mul(float4(input.Pos, 1.0), frameData.Model).xyz;
    output.Normal       = normalize(mul(input.Normal, float3x3(frameData.Model)));
    output.Tangent      = mul(input.Tangent.xyz, float3x3(frameData.Model));
    output.UV           = input.UV;
    output.DrawIndex    = drawIndex;
    output.ViewVec      = -output.Pos.xyz;
    output.LightVec = normalize(frameData.lightPos.xyz - output.WorldPos);
    output.ShadowCoord = mul(float4(input.Pos, 1.0), frameData.depthMVP);

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    // Sample textures
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);
    DrawData draw = LoadBufferArray<DrawData>(pc.PerDrawPointer, input.DrawIndex);

    float3 albedoSample = TextureBindless2D(draw.albedo, 0, input.UV).rgb;
    float2 normalSample = TextureBindless2D(draw.normal, 0, input.UV).rg;
    float4 metallicRoughnessSample = TextureBindless2D(draw.metallicRoughness, 0, input.UV);

    // Material properties
    Material material;
    material.albedo     = albedoSample;
    material.metallic   = metallicRoughnessSample.b;
    material.roughness  = metallicRoughnessSample.g;

    float3 N = GetShadingNormal(input.Normal, input.WorldPos, normalSample, input.UV, input.Tangent);
    float3 V = normalize(frameData.cameraPos - input.WorldPos);
    float3 R = reflect(-V, N);

    // simple PBR lighting
    float3 L = normalize(frameData.lightPos.xyz - input.WorldPos);


    float3 F0 = float3(0.04);
    F0 = lerp(F0, material.albedo, material.metallic);

    float3 Lo = Specular(L, V, N, F0, material);

    float2 brdf = (0.88 * float2(max(dot(N, V), 0.0), material.roughness)).rg;
    float3 reflection = float3(0.3);
    float3 diffuse = 0.7 * material.albedo;
    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, material.roughness);

    float3 kD = 1.0 - F;
	kD *= 1.0 - material.metallic;

    // Specular reflectance
    float3 specular = reflection * (F * brdf.x + brdf.y);
    float3 ambient = (kD * diffuse + specular);
    float3 color = ambient + Lo;

    float3 projCoords = input.ShadowCoord.xyz / input.ShadowCoord.w;

    //If the coord is outside the light frustum don't shadow it.
    if(projCoords.z > 1.0) return float4(color, 1.0);
    
    // Only remap XY -> Vulkan depth already stays in [0, 1].
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // EOS uses a flipped Vulkan viewport, so shadow-map UV Y must be flipped.
    projCoords.y = 1.0 - projCoords.y;

    float bias = max(0.0005 * (1.0 - dot(N, L)), 0.00005);
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2000.0, 2000.0);

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = TextureBindless2D(frameData.shadowMapID, 1, projCoords.xy).r;
            shadow += currentDepth - bias > pcfDepth ? 0.3 : 1.0;
        }
    }
    shadow /= 9.0;

    return float4(color * shadow, 1.0);
}