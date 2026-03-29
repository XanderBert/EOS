module shadowCommon;

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
    public uint     shadowMapID;
    public uint     sceneTLASID;
    public int      shadowDebugMode;
    public int      shadowForceCascade;
    public int      shadowTechnique;
};

public struct DrawData
{
    public uint albedo;
    public uint normal;
    public uint metallicRoughness;
    public uint pad;
    public float4x4 transform;
};

public struct PushConstants
{
    public uint64_t PerFramePointer;
    public uint64_t PerDrawPointer;
};