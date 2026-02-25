import pbr;
import common;

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
};

struct PerFrameData
{
    float4x4 Model;
    float4x4 MVP;
    float3 cameraPos;
    MaterialHandles material;
};

struct PushConstants
{
    uint64_t PerFramePointer;
};

[[vk::push_constant]] PushConstants pc;
[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    VSOutput output;
    output.Pos          = mul(float4(input.Pos, 1.0), frameData.MVP);
    output.WorldPos     = mul(float4(input.Pos, 1.0), frameData.Model).xyz;
    output.Normal       = normalize(mul(input.Normal, float3x3(frameData.Model)));
    output.Tangent      = mul(input.Tangent.xyz, float3x3(frameData.Model));
    output.UV           = input.UV;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
    PerFrameData frameData = LoadBuffer<PerFrameData>(pc.PerFramePointer);

    // Sample textures
    float3 albedoSample = TextureBindless2D(frameData.material.albedo, 0, input.UV).rgb;
    float2 normalSample = TextureBindless2D(frameData.material.normal, 0, input.UV).rg;
    float4 metallicRoughnessSample = TextureBindless2D(frameData.material.metallicRoughness, 0, input.UV);

    // Material properties
    Material material;
    material.albedo     = albedoSample;
    material.metallic   = metallicRoughnessSample.b;
    material.roughness  = metallicRoughnessSample.g;

    float3 N = GetShadingNormal(input.Normal, input.WorldPos, normalSample, input.UV, input.Tangent);
    float3 V = normalize(frameData.cameraPos - input.WorldPos);
    float3 R = reflect(-V, N);

    // simple PBR lighting
    float3 L = normalize(float3(1.0, 1.0, 1.0) - input.WorldPos);


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
    return float4(color, 1.0);
}