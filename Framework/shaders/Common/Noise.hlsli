//Modify Begin:2026-08-06 by Hui
#ifndef FRAMEWORK_COMMON_NOISE_HLSLI
#define FRAMEWORK_COMMON_NOISE_HLSLI

uint FrameworkNoiseHash(uint value)
{
    value ^= value >> 17u;
    value *= 0xed5ad4bbu;
    value ^= value >> 11u;
    value *= 0xac4c1b51u;
    value ^= value >> 15u;
    value *= 0x31848babu;
    value ^= value >> 14u;
    return value;
}

float FrameworkNoiseHash01(const uint value)
{
    return (float(FrameworkNoiseHash(value)) + 0.5f) / 4294967296.0f;
}

float2 FrameworkNoiseHash02(const uint2 value, const uint salt)
{
    const uint x = FrameworkNoiseHash(value.x ^ (value.y * 0x9e3779b9u) ^ salt);
    const uint y = FrameworkNoiseHash(value.y ^ (value.x * 0x85ebca6bu) ^ (salt * 0xc2b2ae35u));
    return float2(FrameworkNoiseHash01(x), FrameworkNoiseHash01(y));
}

float FrameworkInterleavedGradientNoise(const float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float FrameworkAnimatedInterleavedGradientNoise(const uint2 pixel, const uint frameIndex)
{
    const float temporalOffset = float(frameIndex & 63u) * 5.588238f;
    return FrameworkInterleavedGradientNoise(float2(pixel) + temporalOffset);
}

float2 FrameworkCranleyPattersonRotate(const float2 sample, const float2 rotation)
{
    return frac(sample + rotation);
}

float2 FrameworkInterleavedGradientNoise2D(const uint2 pixel, const uint frameIndex, const uint salt)
{
    const uint2 scrambledPixel = pixel ^ uint2(salt, salt * 0x9e3779b9u);
    const float temporalOffset = float(frameIndex & 63u) * 5.588238f;
    const float2 ignSample = float2(
        FrameworkInterleavedGradientNoise(float2(scrambledPixel) + temporalOffset),
        FrameworkInterleavedGradientNoise(float2(scrambledPixel.y, scrambledPixel.x) + temporalOffset + 19.1904f));
    const float2 rotation = FrameworkNoiseHash02(pixel ^ uint2(frameIndex, frameIndex * 0x27d4eb2du), salt);
    return FrameworkCranleyPattersonRotate(ignSample, rotation);
}

#endif
//Modify End
