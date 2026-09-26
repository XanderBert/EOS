module shadowCommon;

public struct PerFrameData
{
    public float4x4 Model;
    public float4x4 MVP;
    public float4x4 depthMVP;
    public float4   lightPos;
    public float4   lightDir; // xyz = direction the light travels (directional light)
    public float3   cameraPos;
    public uint     shadowMapID;
};

public struct PushConstants
{
    public uint64_t PerFramePointer;
    public uint64_t PerDrawPointer;
};