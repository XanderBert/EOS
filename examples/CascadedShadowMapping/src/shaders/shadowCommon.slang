module shadowCommon;

import eos.material;

public static const int SHADOW_TECHNIQUE_CPU_CASCADE = 0;
public static const int SHADOW_TECHNIQUE_COMPUTE_CASCADE = 1;
public static const int SHADOW_TECHNIQUE_RAY_QUERY = 2;

public struct PerFrameData
{
    public float4x4 Model;
    public float4x4 MVP;
    public float4x4 View;
    public float4x4 cascadeViewProj[4];
    public float4   lightDir;
    public float4   cascadeSplits;
    public float3   cameraPos;
    public DescriptorHandle<Texture2DArray>                   shadowMap;
    public DescriptorHandle<SamplerState>                     shadowSampler;
    public DescriptorHandle<RaytracingAccelerationStructure>  sceneTLAS;       // invalid when the device has no ray tracing
    public int      shadowDebugMode;
    public int      shadowForceCascade;
    public int      shadowTechnique;
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
