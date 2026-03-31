module fullscreen;

public void FullscreenTriangle(uint id, out float2 uv, out float4 pos)
{
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
}
