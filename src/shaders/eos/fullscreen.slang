// A triangle that covers the screen, for passes that shade every pixel.
module "eos/fullscreen";

public void FullscreenTriangle(uint id, out float2 uv, out float4 pos)
{
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

// What the vertex shader below hands a fragment shader: uv runs from (0, 0) at the top left to (1, 1).
public struct FullscreenVaryings
{
    public float4 position : SV_Position;
    public float2 uv       : TEXCOORD0;
};

// The vertex shader of raster passes written in Slang (eos.pass) that have none of their own: draw 3 vertices.
[shader("vertex")]
FullscreenVaryings vertexMain(uint id : SV_VertexID)
{
    FullscreenVaryings output;
    FullscreenTriangle(id, output.uv, output.position);
    return output;
}
