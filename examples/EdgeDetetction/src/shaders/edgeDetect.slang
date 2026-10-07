// Darkens the lit scene along edges, which a Sobel filter finds in the G-buffer normals.
import eos.core;
import eos.math;
import eos.bindless;
import eos.fullscreen;
import eos.pass;

[Pass]
struct EdgeDetect
{
    [Input] DescriptorHandle<Texture2D> color;
    [Input] DescriptorHandle<Texture2D> normal;
    DescriptorHandle<SamplerState>      linearSampler;

    [Range(2.0, 8.0)] float threshold = 4.0;
    bool edgesOnly = false;
};

[[vk::push_constant]] EdgeDetect pass;

struct Targets
{
    [Output] float4 output : SV_Target0;
};

float3 SampleNormal(float2 uv)
{
    return UnpackNormalUnorm(pass.normal.Sample(pass.linearSampler, uv).rgb);
}

[shader("fragment")]
Targets fragmentMain(FullscreenVaryings input)
{
    float2 uv = input.uv;
    uv.y = 1 - uv.y;

    uint2 colorSize;
    pass.color.GetDimensions(colorSize.x, colorSize.y);
    const float2 ts = 1.0 / float2(colorSize);

    const float3 tl = SampleNormal(uv + float2(-1.0,  1.0) * ts);
    const float3 tm = SampleNormal(uv + float2( 0.0,  1.0) * ts);
    const float3 tr = SampleNormal(uv + float2( 1.0,  1.0) * ts);
    const float3 ml = SampleNormal(uv + float2(-1.0,  0.0) * ts);
    const float3 mr = SampleNormal(uv + float2( 1.0,  0.0) * ts);
    const float3 bl = SampleNormal(uv + float2(-1.0, -1.0) * ts);
    const float3 bm = SampleNormal(uv + float2( 0.0, -1.0) * ts);
    const float3 br = SampleNormal(uv + float2( 1.0, -1.0) * ts);

    const float3 gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
    const float3 gy =  tl + 2.0 * tm + tr - bl - 2.0 * bm - br;
    const float edge = sqrt(dot(gx, gx) + dot(gy, gy));

    Targets targets;
    if (pass.edgesOnly)
    {
        const float e = step(pass.threshold, edge);
        targets.output = float4(e, e, e, 1.0);
        return targets;
    }

    uv.y = 1 - uv.y;
    const float4 litScene = pass.color.Sample(pass.linearSampler, uv);
    const float edgeMask = 1.0 - saturate((edge - pass.threshold) * 10.0);
    targets.output = float4(litScene.rgb * edgeMask, 1.0);
    return targets;
}
