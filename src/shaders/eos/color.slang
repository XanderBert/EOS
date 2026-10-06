// Color space conversions.
module "eos/color";

public float Luminance(float3 linearRgb)
{
    return dot(linearRgb, float3(0.2126, 0.7152, 0.0722));
}

// The exact sRGB transfer functions (IEC 61966-2-1), not the 2.2 gamma approximation.
public float3 SrgbToLinear(float3 srgb)
{
    const float3 low = srgb / 12.92;
    const float3 high = pow((srgb + 0.055) / 1.055, 2.4);
    return select(srgb <= 0.04045, low, high);
}

public float3 LinearToSrgb(float3 linearRgb)
{
    const float3 low = linearRgb * 12.92;
    const float3 high = 1.055 * pow(linearRgb, 1.0 / 2.4) - 0.055;
    return select(linearRgb <= 0.0031308, low, high);
}
