// What the cascaded shadow passes share.
module shadowCommon;

import eos.core;

public static const uint kCascadeCount = 4;
public static const int kShadowMapSize = 4096;      // the cpuCascades node in main.cpp snaps to this size as well

// The cascades the shadow map is rendered with: from the GPU fit (cascadeSetup.slang) or the CPU one (main.cpp).
[CppExport]
public struct CascadeData
{
    public float4x4 viewProjection[kCascadeCount];  // world to the light's clip space, per cascade
    public float4 splits;                           // view-space distance where each cascade ends
};

// Depth range of the visible scene: written by depthReduction.slang, read by cascadeSetup.slang.
[CppExport]
public struct DepthReductionData
{
    public float minDepth;
    public float maxDepth;
    public float pad0;
    public float pad1;
};
