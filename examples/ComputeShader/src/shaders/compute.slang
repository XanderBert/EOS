import eos.core;

[CppExport]
public struct ComputePayload
{
    public uint lhs;
    public uint rhs;
    public uint result;
    public uint pad;
};

[CppExport]
public struct ComputePushConstants
{
    public ComputePayload* payload;
};

[[vk::push_constant]] ComputePushConstants pc;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x != 0 || dispatchThreadID.y != 0 || dispatchThreadID.z != 0)
    {
        return;
    }

    ComputePayload payload = *pc.payload;
    payload.result = payload.lhs * payload.rhs;
    *pc.payload = payload;
}
