module shadowCommon;

public struct PerFrameData
{
    public float4x4 Model;
    public float4x4 MVP;
    public float4x4 View;
    public float4x4 cascadeViewProj[4];
    public float4   lightPos;
    public float4   cascadeSplits;
    public float3   cameraPos;
    public uint     shadowMapID;
    public bool     showCascades;
};

public struct PushConstants
{
    public uint64_t PerFramePointer;
    public uint64_t PerDrawPointer;
    public uint     cascadeID;
    public uint     _pad;
};