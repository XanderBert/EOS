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

groupshared float gsMinDepth[256];
groupshared float gsMaxDepth[256];

[numthreads(16, 16, 1)]
[shader("compute")]
void computeMain(uint3 groupThreadID : SV_GroupThreadID)
{
    // Get the pixel ID
    uint localIndex = groupThreadID.y * 16 + groupThreadID.x;


    float minDepth = 1.0;
    float maxDepth = 0.0;

    [loop]
    for (uint y = groupThreadID.y; y < pc.height; y += 16)
    {
        [loop]
        for (uint x = groupThreadID.x; x < pc.width; x += 16)
        {
            float depth = TextureBindless2DLoad(pc.depthTextureID, int2(x, y)).r;

            // Skip untouched clear values so cascades fit actual scene depth.
            if (depth >= 0.999999)
            {
                continue;
            }

            minDepth = min(minDepth, depth);
            maxDepth = max(maxDepth, depth);
        }
    }

    // If no valid depth was found (minDepth > maxDepth), use a reasonable default
    // to avoid degenerate cascade frustums. This handles sparse geometry scenes.
    if (minDepth > maxDepth)
    {
        // Use broad range from near to far to cover full view frustum
        minDepth = 0.01;
        maxDepth = 0.95;
    }

    gsMinDepth[localIndex] = minDepth;
    gsMaxDepth[localIndex] = maxDepth;
    GroupMemoryBarrierWithGroupSync();

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
