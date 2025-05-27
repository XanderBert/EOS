// Fragment Shader

struct FragmentInput
{
    float4 position : SV_Position; // Not typically used directly in basic fragment shaders
    float3 color    : COLOR;       // Input color from vertex shader
};

float4 main(FragmentInput input) : SV_Target
{
    // Output the interpolated color from the vertex shader
    return float4(input.color, 1.0); // r, g, b, alpha
}