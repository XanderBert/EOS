// Resets the depth range to the identity of the atomic min/max the depth reduction does: min at the far plane, max at
// the near plane.
import eos.core;
import eos.pass;
import shadowCommon;

[Pass]
[DispatchThreads(1, 1, 1)]
struct ResetDepthRange
{
    [Output] DepthReductionData* depthRange;
};

[[vk::push_constant]] ResetDepthRange pass;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain()
{
    DepthReductionData sentinel;
    sentinel.minDepth = 1.0;
    sentinel.maxDepth = 0.0;
    sentinel.pad0 = 0.0;
    sentinel.pad1 = 0.0;
    *pass.depthRange = sentinel;
}
