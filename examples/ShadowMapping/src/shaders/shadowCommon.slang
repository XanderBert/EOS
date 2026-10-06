module shadowCommon;

import eos.core;
import eos.material;

[CppExport]
public struct PerFrameData
{
    public float4x4 model;
    public float4x4 mvp;
    public float4x4 depthMVP;
    public float4   lightPos;
    public float4   lightDir; // xyz = direction the light travels (directional light)
    public float3   cameraPos;
    public DescriptorHandle<Texture2D>    shadowMap;
    public DescriptorHandle<SamplerState> shadowSampler;
};

[CppExport]
public struct DrawData
{
    public float4x4 transform;
    public StandardMaterialData material;
};

// Push constants of the shadow and shade passes.
[CppExport]
public struct FramePointers
{
    public PerFrameData* perFrame;
    public DrawData* draws;
};
