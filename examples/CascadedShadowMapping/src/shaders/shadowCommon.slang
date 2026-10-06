module shadowCommon;

import eos.core;
import eos.material;

public enum ShadowTechnique : int
{
    CpuCascade = 0,
    ComputeCascade = 1,
    RayQuery = 2,
}

[CppExport]
public struct PerFrameData
{
    public float4x4 model;
    public float4x4 mvp;
    public float4x4 view;
    public float4x4 cascadeViewProj[4];
    public float4   lightDir;
    public float4   cascadeSplits;
    public float3   cameraPos;
    public DescriptorHandle<Texture2DArray>                   shadowMap;
    public DescriptorHandle<SamplerState>                     shadowSampler;
    public DescriptorHandle<RaytracingAccelerationStructure>  sceneTLAS;       // invalid when the device has no ray tracing
    public int      shadowDebugMode;
    public int      shadowForceCascade;
    public ShadowTechnique shadowTechnique;
};

[CppExport]
public struct DrawData
{
    public float4x4 transform;
    public StandardMaterialData material;
};

// Push constants of the early-Z, shadow and shade passes.
[CppExport]
public struct FramePointers
{
    public PerFrameData* perFrame;
    public DrawData* draws;
};

// Depth range of the visible scene: written by depthReduction.slang, read by cascadeSetup.slang.
[CppExport]
public struct DepthReductionData
{
    public float minDepth;
    public float maxDepth;
    public float pad0;
    public float pad1;
};
