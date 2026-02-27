module shadowCommon;

public struct PerFrameData
{
    public float4x4 Model;
    public float4x4 MVP;
    public float4x4 depthMVP;
    public float4   lightPos;
    public float3   cameraPos;
    public uint     shadowMapID;
    public uint64_t drawDataPtr;
};

public struct PushConstants
{
    public uint64_t PerFramePointer;
};