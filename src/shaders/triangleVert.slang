// Vertex Shader

struct VertexInput
{
    float2 position : POSITION; // Input vertex position
    float3 color    : COLOR;    // Input vertex color
};

struct VertexOutput
{
    float4 position : SV_Position; // Output clip-space position
    float3 color    : COLOR;       // Output color to fragment shader
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    // Pass the 2D position directly, assuming it's already in clip space for simplicity.
    // z = 0.5 for visibility, w = 1.0 for a proper perspective divide.
    output.position = float4(input.position, 0.5, 1.0);
    output.color = input.color;
    return output;
}