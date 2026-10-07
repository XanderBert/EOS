import eos.core;
import eos.pass;

[CppExport]
public struct ComputePayload
{
    public uint lhs;
    public uint rhs;
    public uint result;
    public uint pad;
};

// Multiplies the payload's two numbers. It only has a buffer, so it says how many threads it dispatches.
[Pass]
[DispatchThreads(1, 1, 1)]
struct ComputeValidation
{
    [InOut] ComputePayload* payload;
};

[[vk::push_constant]] ComputeValidation pass;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    ComputePayload payload = *pass.payload;
    payload.result = payload.lhs * payload.rhs;
    *pass.payload = payload;
}
