import eos.bindless;
import eos.core;
import eos.pass;
import shadowCommon;

// Finds the depth range of the visible scene: every 16x16 group reduces its pixels to one min/max in shared memory and a
// wave, and one lane folds it into the result with atomics. The dispatch is one thread per pixel of the depth.
[Pass]
[DispatchSizeOf("depth")]
struct DepthReduction
{
    [Input] DescriptorHandle<Texture2D> depth;
    [InOut] DepthReductionData*         depthRange;     // reset to {1, 0} by resetDepthRange before this pass
};

[[vk::push_constant]] DepthReduction pass;

// Two Stage strategy:
// each group reduces its assigned tiles into one result,
// then lane 0 does an atomic min/max into the output buffer.

groupshared float gsMinDepth[256];
groupshared float gsMaxDepth[256];


[numthreads(16, 16, 1)]
[shader("compute")]
void computeMain(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint  localIndex       : SV_GroupIndex)
{
    // ---------------------------------------------------------------------------
    // Phase 1 — each thread checks its own pixel
    // ---------------------------------------------------------------------------

    uint2 depthSize;
    pass.depth.GetDimensions(depthSize.x, depthSize.y);

    float lMin = 1.0;
    float lMax = 0.0;

    if (all(dispatchThreadID.xy < depthSize))
    {
        float d = pass.depth.Load(int3(dispatchThreadID.xy, 0)).r;
        if (d < 1.0) // skip clear / sky pixels
        {
            lMin = d;
            lMax = d;
        }
    }

    // thread only saw sky/clear pixels.
    if (lMin > lMax) { lMin = 1.0; lMax = 0.0; }

    gsMinDepth[localIndex] = lMin;
    gsMaxDepth[localIndex] = lMax;

    GroupMemoryBarrierWithGroupSync();

    // ---------------------------------------------------------------------------
    // Phase 2 — standard LDS reduction down to 32 threads. (1 warp)
    // ---------------------------------------------------------------------------

    // each group of 256 threads reduces to 128
    if (localIndex < 128)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 128]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 128]);
    }
    GroupMemoryBarrierWithGroupSync();

    // then 128 to 64
    if (localIndex < 64)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 64]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 64]);
    }
    GroupMemoryBarrierWithGroupSync();

    // then 64 to 32
    if (localIndex < 32)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 32]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 32]);
    }
    GroupMemoryBarrierWithGroupSync();

    // ---------------------------------------------------------------------------
    // Phase 3 — warp reduction via wave intrinsics. No barriers needed inside a single warp
    // https://github.com/microsoft/directxshadercompiler/wiki/wave-intrinsics
    // ---------------------------------------------------------------------------
    if (localIndex < 32)
    {
        // As GPU lanes are typically grouped in 32s or less, and we have 32 or fewer lanes left at this point,
        // this wave reduction will cover all remaining lanes in the group.
        float waveMin = WaveActiveMin(gsMinDepth[localIndex]);
        float waveMax = WaveActiveMax(gsMaxDepth[localIndex]);

        // after setting all lanes to the same value,
        // we only want one lane to write to the output buffer, so we pick lane 0.
        if (WaveIsFirstLane())
        {
            Ptr<DepthReductionData> outPtr = pass.depthRange;

            // Atomic float min/max via uint bit-cast. Depths are non-negative, and non-negative
            // IEEE floats sort identically to their uint bits, so no bit flipping is needed and the
            // buffer holds plain floats (reset to {1.0, 0.0} by resetDepthRange, read as floats in cascadeSetup).
            // https://www.jeremyong.com/graphics/2023/09/05/f32-interlocked-min-max-hlsl/
            InterlockedMin(((Ptr<uint>)outPtr)[0], asuint(waveMin)); // performs an atomic min of value to memory
            InterlockedMax(((Ptr<uint>)outPtr)[1], asuint(waveMax)); // performs an atomic max of value to memory
        }
    }
}