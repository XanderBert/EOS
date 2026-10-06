module shadowCommon;

import eos.material;

public struct PerFrameData
{
    public float4x4 Model;
    public float4x4 MVP;
    public float4x4 depthMVP;
    public float4   lightPos;
    public float4   lightDir; // xyz = direction the light travels (directional light)
    public float3   cameraPos;
    public DescriptorHandle<Texture2D>    shadowMap;
    public DescriptorHandle<SamplerState> shadowSampler;
};

public struct DrawData
{
    public float4x4 transform;
    public StandardMaterialData material;
};

public struct PushConstants
{
    public PerFrameData* perFrame;
    public DrawData* draws;
};
