// Materials: turn the textures and parameters at a surface point into a BSDF, emission and coverage.
//
// A material is a type implementing IMaterial. The rasterizer specializes its shaders per material type (generics, no
// runtime dispatch); a shader graph produces new IMaterial types. Texture reads go through an ITextureSampler, so the
// same material works in fragment shaders (hardware mip selection) and in compute or ray tracing stages (explicit level).
module "eos/material";

import eos.core;
import eos.math;
import eos.bindless;
import eos.bsdf;

// How a material reads its textures.
public interface ITextureSampler
{
    float4 Sample(DescriptorHandle<Texture2D> texture, DescriptorHandle<SamplerState> samplerState, float2 uv);
}

// Mip level from screen-space derivatives. Fragment shaders only.
public struct ImplicitLodSampler : ITextureSampler
{
    public float4 Sample(DescriptorHandle<Texture2D> texture, DescriptorHandle<SamplerState> samplerState, float2 uv)
    {
        return texture.Sample(samplerState, uv);
    }
}

// A given mip level, for stages without derivatives (compute, ray tracing).
public struct ExplicitLodSampler : ITextureSampler
{
    public float lod;

    public __init(float lod)
    {
        this.lod = lod;
    }

    public float4 Sample(DescriptorHandle<Texture2D> texture, DescriptorHandle<SamplerState> samplerState, float2 uv)
    {
        return texture.SampleLevel(samplerState, uv, lod);
    }
}

// The geometry at the point being shaded, in world space.
public struct SurfaceData
{
    public float3 position;
    public float3 geometricNormal;  // true surface normal; the interpolated normal when it is not known (rasterization)
    public float3 normal;           // interpolated vertex normal, unit length
    public float4 tangent;          // xyz: interpolated vertex tangent, w: bitangent sign (glTF), 0 when the mesh has none
    public float2 uv;
    public float3 viewDirection;    // unit vector from the surface towards the viewer

    // From interpolated vertex attributes in a fragment shader. The interpolated normal doubles as the geometric
    // normal; the tangent and normal need not be unit length.
    public static SurfaceData FromVertex(float3 position, float3 normal, float4 tangent, float2 uv, float3 cameraPosition)
    {
        SurfaceData surface;
        surface.position = position;
        surface.normal = normalize(normal);
        surface.geometricNormal = surface.normal;
        surface.tangent = tangent;
        surface.uv = uv;
        surface.viewDirection = normalize(cameraPosition - position);
        return surface;
    }

    // Only a texture coordinate, for passes that test coverage and nothing else (IMaterial.EvaluateOpacity).
    public static SurfaceData FromUV(float2 uv)
    {
        SurfaceData surface;
        surface.position = float3(0.0);
        surface.geometricNormal = float3(0.0, 0.0, 1.0);
        surface.normal = float3(0.0, 0.0, 1.0);
        surface.tangent = float4(0.0);
        surface.uv = uv;
        surface.viewDirection = float3(0.0, 0.0, 1.0);
        return surface;
    }
}

// A material evaluated at one point.
public struct MaterialPoint<B : IBSDF>
{
    public B bsdf;
    public ShadingFrame frame;      // the BSDF's local frame; N is the normal-mapped shading normal
    public float3 emission;         // emitted radiance
    public float opacity;           // coverage in [0, 1], for alpha testing and blending
    public float occlusion;         // baked ambient occlusion (1: none); only meant for approximate ambient light
}

public interface IMaterial
{
    associatedtype BSDF : IBSDF;

    MaterialPoint<BSDF> Evaluate<S : ITextureSampler>(SurfaceData surface, S textureSampler);

    // Coverage alone, for passes that need nothing else: depth pre-passes, shadow maps, any-hit shaders.
    // Such passes may only know part of the surface (typically position and uv).
    float EvaluateOpacity<S : ITextureSampler>(SurfaceData surface, S textureSampler);

    // False where the material is cut away (glTF MASK below the cutoff). Every pass that writes depth or casts
    // shadows has to apply it, or the holes become opaque.
    bool PassesAlphaTest(float opacity);
}

// glTF alphaMode.
public static const uint kAlphaModeOpaque = 0;
public static const uint kAlphaModeMask   = 1;
public static const uint kAlphaModeBlend  = 2;

// Parameters of a glTF 2.0 core metallic-roughness material. Every texture is optional: an invalid handle
// (EOS::DescriptorHandle{} on the C++ side) means the factor alone is used. Color textures must be created with an
// sRGB format, so sampling returns linear values.
public struct StandardMaterialData
{
    public float4 baseColorFactor;                                 // linear RGB, A = alpha
    public float3 emissiveFactor;                                  // linear RGB
    public float metallicFactor;
    public float roughnessFactor;
    public float normalScale;
    public float occlusionStrength;
    public float alphaCutoff;                                      // only used with kAlphaModeMask
    public uint alphaMode;                                         // kAlphaMode*

    public DescriptorHandle<Texture2D> baseColorTexture;           // RGB color, A alpha
    public DescriptorHandle<Texture2D> metallicRoughnessTexture;   // G roughness, B metallic
    public DescriptorHandle<Texture2D> normalTexture;              // tangent-space normal in RG (two-channel) or RGB
    public DescriptorHandle<Texture2D> emissiveTexture;
    public DescriptorHandle<Texture2D> occlusionTexture;           // R occlusion
    public DescriptorHandle<SamplerState> samplerState;
}

// glTF 2.0 core metallic-roughness material.
public struct StandardMaterial : IMaterial
{
    public StandardMaterialData data;

    public typealias BSDF = StandardBSDF;

    public __init(StandardMaterialData data)
    {
        this.data = data;
    }

    public MaterialPoint<StandardBSDF> Evaluate<S : ITextureSampler>(SurfaceData surface, S textureSampler)
    {
        float4 baseColor = data.baseColorFactor;
        if (IsValid(data.baseColorTexture)) baseColor *= textureSampler.Sample(data.baseColorTexture, data.samplerState, surface.uv);

        float metallic = data.metallicFactor;
        float roughness = data.roughnessFactor;
        if (IsValid(data.metallicRoughnessTexture))
        {
            const float4 metallicRoughness = textureSampler.Sample(data.metallicRoughnessTexture, data.samplerState, surface.uv);
            roughness *= metallicRoughness.g;
            metallic *= metallicRoughness.b;
        }

        ShadingFrame frame = ShadingFrame.FromNormalAndTangent(surface.normal, surface.tangent);
        if (IsValid(data.normalTexture))
        {
            // Rebuild z from xy so two-channel (BC5) normal maps work too, then scale xy as glTF specifies.
            const float2 xy = textureSampler.Sample(data.normalTexture, data.samplerState, surface.uv).xy * 2.0 - 1.0;
            const float z = sqrt(saturate(1.0 - dot(xy, xy)));
            const float3 shadingNormal = normalize(frame.FromLocal(float3(xy * data.normalScale, z)));
            frame = ShadingFrame.FromNormalAndTangent(shadingNormal, float4(frame.T, surface.tangent.w));
        }

        MaterialPoint<StandardBSDF> point;
        point.bsdf = StandardBSDF(baseColor.rgb, metallic, roughness);
        point.frame = frame;

        point.emission = data.emissiveFactor;
        if (IsValid(data.emissiveTexture)) point.emission *= textureSampler.Sample(data.emissiveTexture, data.samplerState, surface.uv).rgb;

        point.opacity = data.alphaMode == kAlphaModeOpaque ? 1.0 : baseColor.a;

        point.occlusion = 1.0;
        if (IsValid(data.occlusionTexture))
        {
            const float occlusion = textureSampler.Sample(data.occlusionTexture, data.samplerState, surface.uv).r;
            point.occlusion = lerp(1.0, occlusion, data.occlusionStrength);
        }

        return point;
    }

    public float EvaluateOpacity<S : ITextureSampler>(SurfaceData surface, S textureSampler)
    {
        if (data.alphaMode == kAlphaModeOpaque) return 1.0;

        float opacity = data.baseColorFactor.a;
        if (IsValid(data.baseColorTexture)) opacity *= textureSampler.Sample(data.baseColorTexture, data.samplerState, surface.uv).a;
        return opacity;
    }

    // glTF MASK: fragments below the cutoff are discarded (fragment shaders) or ignored (any-hit shaders).
    public bool PassesAlphaTest(float opacity)
    {
        return data.alphaMode != kAlphaModeMask || opacity >= data.alphaCutoff;
    }
}
