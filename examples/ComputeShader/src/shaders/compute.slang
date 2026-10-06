struct ComputePayload
{
    uint lhs;
    uint rhs;
    uint result;
    uint pad;
};

struct PushConstants
{
    ComputePayload* payload;
};

[[vk::push_constant]] PushConstants pc;

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
