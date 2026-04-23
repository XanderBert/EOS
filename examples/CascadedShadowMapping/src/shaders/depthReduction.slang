import bindings;

struct DepthReductionData
{
    float minDepth;
    float maxDepth;
    float pad0;
    float pad1;
};

struct PushConstants
{
    uint64_t depthRangePtr;
    uint      depthTextureID;
    uint      width;
    uint      height;
    uint      numGroupsX;
};

[[vk::push_constant]] PushConstants pc;

// Two Stage strategy:
// each group reduces its assigned tiles into one result,
// then lane 0 does an atomic min/max into the output buffer.

groupshared float gsMinDepth[256];
groupshared float gsMaxDepth[256];


[numthreads(256, 1, 1)]
[shader("compute")]
void computeMain(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupID          : SV_GroupID,
    uint  localIndex       : SV_GroupIndex)
{
    // ---------------------------------------------------------------------------
    // Phase 1 — each thread checks its own pixel
    // ---------------------------------------------------------------------------

    // Map flat 256-thread group back to a 16x16 logical tile position.
    uint lx = localIndex % 16;
    uint ly = localIndex / 16;

    // This group's starting pixel in the texture.
    uint startX = groupID.x * 16 + lx;
    uint startY = groupID.y * 16 + ly;

    // Stride by the full dispatch so groups tile the image.
    uint strideX = pc.numGroupsX * 16;

    float lMin = 1.0;
    float lMax = 0.0;

    // X stride loop handles widths that aren't a multiple of (numGroupsX * 16).
    for (uint x = startX; x < pc.width; x += strideX)
    {
        uint y = startY;
        if (y < pc.height)
        {
            float d = TextureBindless2DLoad(pc.depthTextureID, int2(x, y)).r;
            if (d < 1.0) // skip clear / sky pixels
            {
                lMin = min(lMin, d);
                lMax = max(lMax, d);
            }
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
            Ptr<DepthReductionData> outPtr = (Ptr<DepthReductionData>)pc.depthRangePtr;

            // Atomic float min/max via bit-cast tricks (valid for [0,1] depths):
            //https://www.jeremyong.com/graphics/2023/09/05/f32-interlocked-min-max-hlsl/

            // minDepth atomic: flip bits so uint order matches inverse float order.
            uint minBits = ~asuint(waveMin);
            InterlockedMin(((Ptr<uint>)outPtr)[0], minBits); // performs an atomic min of value to memory

            // maxDepth atomic: [0,1] floats sort identically to their uint bits.
            uint maxBits = asuint(waveMax);
            InterlockedMax(((Ptr<uint>)outPtr)[1], maxBits); // performs an atomic max of value to memory
        }
    }
}