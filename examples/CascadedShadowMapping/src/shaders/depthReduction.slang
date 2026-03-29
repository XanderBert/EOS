import common;

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
    uint depthTextureID;
    uint depthSamplerID;
    uint width;
    uint height;
};

[[vk::push_constant]] PushConstants pc;

// These arrays live in thread-group shared memory (on-chip LDS/shared memory).
// Every thread writes its local min/max once, then the group reduces to a
// single min/max pair using synchronized reduction steps below.
groupshared float gsMinDepth[256];
groupshared float gsMaxDepth[256];

[numthreads(16, 16, 1)]
[shader("compute")]
void computeMain(uint3 groupThreadID : SV_GroupThreadID)
{
    // Flatten 2D thread-id into [0, 255] index for our shared arrays.
    uint localIndex = groupThreadID.y * 16 + groupThreadID.x;


    // Start with identity values for min/max reduction.
    float minDepth = 1.0;
    float maxDepth = 0.0;

    // This shader runs as one 16x16 group. Each lane strided-walks the full
    // depth texture so together they cover all pixels and build local min/max.
    [loop]
    for (uint y = groupThreadID.y; y < pc.height; y += 16)
    {
        [loop]
        for (uint x = groupThreadID.x; x < pc.width; x += 16)
        {
            float depth = TextureBindless2DLoad(pc.depthTextureID, int2(x, y)).r;

            // Skip only true clear values. Using a loose threshold can incorrectly
            // drop valid far geometry that naturally lands very close to 1.0.
            if (depth >= 1.0)
            {
                continue;
            }

            minDepth = min(minDepth, depth);
            maxDepth = max(maxDepth, depth);
        }
    }

    // If no valid depth was found (minDepth > maxDepth), fall back to full depth range.
    if (minDepth > maxDepth)
    {
        minDepth = 0.0;
        maxDepth = 1.0;
    }

    // Publish each lane's local result into group-shared memory.
    gsMinDepth[localIndex] = minDepth;
    gsMaxDepth[localIndex] = maxDepth;

    // Barrier + sync is required so all threads see fully written shared data
    // before any lane starts reading neighbors for reduction.
    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction in stages: 256 -> 128 -> 64.
    if (localIndex < 128)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 128]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 128]);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localIndex < 64)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 64]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 64]);
    }
    GroupMemoryBarrierWithGroupSync();

    // Final reduction inside first warp (32 lanes). This is unrolled to keep
    // overhead low and reduce branch/control cost.
    if (localIndex < 32)
    {
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 32]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 32]);
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 16]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 16]);
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 8]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 8]);
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 4]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 4]);
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 2]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 2]);
        gsMinDepth[localIndex] = min(gsMinDepth[localIndex], gsMinDepth[localIndex + 1]);
        gsMaxDepth[localIndex] = max(gsMaxDepth[localIndex], gsMaxDepth[localIndex + 1]);
    }

    GroupMemoryBarrierWithGroupSync();

    // Lane 0 owns the final reduced result and writes it to the output buffer.
    if (localIndex != 0)
    {
        return;
    }

    DepthReductionData result;
    result.minDepth = gsMinDepth[0];
    result.maxDepth = gsMaxDepth[0];
    result.pad0 = 0.0;
    result.pad1 = 0.0;
    *((Ptr<DepthReductionData>)pc.depthRangePtr) = result;
}
