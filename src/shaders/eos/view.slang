// A point of view of a frame: what a camera node (flyCamera) hands the passes on its View* pin.
module "eos/view";

import eos.core;

// Matrices are applied as mul(v, M), like every matrix EOS hands shaders.
[CppExport]
public struct View
{
    public float4x4 view;                   // world to view
    public float4x4 projection;             // view to clip
    public float4x4 viewProjection;         // world to clip
    public float4x4 inverseViewProjection;  // clip to world
    public float4x4 previousViewProjection; // last frame's world to clip, for temporal effects (this frame's on the first)
    public float3 position;                 // of the camera, in world space
    public float nearPlane;
    public float3 forward;                  // unit direction the camera looks in
    public float farPlane;
    public float2 resolution;               // of the swapchain, in pixels
    public float time;                      // seconds since the camera was created
    public float deltaTime;                 // seconds since the previous frame
    public uint frameIndex;
};
