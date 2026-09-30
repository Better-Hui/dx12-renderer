#ifndef RAYTRACING_DEMO_PATH_TRACING_RANDOM_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_RANDOM_HLSLI

#include "../Common/PathTracingConstants.hlsli"
//Modify Begin:2026-08-06 by Hui
#include <Common/BlueNoise.hlsli>
//Modify End

uint Hash(uint value)
{
    value ^= value >> 17;
    value *= 0xed5ad4bbu;
    value ^= value >> 11;
    value *= 0xac4c1b51u;
    value ^= value >> 15;
    value *= 0x31848babu;
    value ^= value >> 14;
    return value;
}

//Modify Begin:2026-09-30 by Hui
float Random01(inout uint state)
{
    // The state stores a 128x128 STBN pixel, a 64-frame STBN slice, and a
    // dimension cursor. Every dimension remains STBN-driven; there is no
    // pseudo-random fallback for later dimensions.
    const uint2 pixel = uint2(
        state & (FrameworkBlueNoiseTileSize - 1u),
        (state >> 7u) & (FrameworkBlueNoiseTileSize - 1u));
    const uint frameIndex = (state >> 14u) & (FrameworkBlueNoiseFrameCount - 1u);
    const uint dimension = state >> 20u;
    const uint dimensionSalt = 0x9e3779b9u ^ (dimension * 0x85ebca6bu);
    state = (state & 0x000fffffu) | (((dimension + 1u) & 0xfffu) << 20u);
    return FrameworkSampleStbnScalar(pixel, frameIndex, dimensionSalt);
}
//Modify End

float HashToFloat(uint value)
{
    return float(Hash(value) & 0x00ffffffu) / 16777216.0f;
}

float InterleavedGradientNoise(float2 pixel)
{
    return FrameworkSampleStbnScalar(uint2(max(pixel, 0.0f)), 0u, 0x1f123bb5u);
}

float AnimatedInterleavedGradientNoise(uint2 pixel, uint frameIndex)
{
    return FrameworkSampleStbnScalar(pixel, frameIndex, 0x1f123bb5u);
}

uint InitializeRandomState(uint2 pixel, uint width, uint frameIndex, uint salt)
{
//Modify Begin:2026-09-30 by Hui
    (void)width;
    const uint2 saltedPixel = pixel + FrameworkBlueNoiseSaltPixelOffset(salt);
    const uint saltedFrame = frameIndex + FrameworkBlueNoiseSaltFrameOffset(salt);
    return
        (saltedPixel.x & (FrameworkBlueNoiseTileSize - 1u)) |
        ((saltedPixel.y & (FrameworkBlueNoiseTileSize - 1u)) << 7u) |
        ((saltedFrame & (FrameworkBlueNoiseFrameCount - 1u)) << 14u);
//Modify End
}

void BuildOrthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 ToWorldHemisphere(float3 normal, float x, float y, float z)
{
    float3 tangent;
    float3 bitangent;
    BuildOrthonormalBasis(normal, tangent, bitangent);
    return normalize(tangent * x + bitangent * y + normal * z);
}

//Modify Begin:2026-07-30 by Hui
float3 SampleCosineHemisphere(float3 normal, const float2 sample)
{
    float r = sqrt(sample.x);
    float phi = 2.0f * PI * sample.y;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0f, 1.0f - sample.x));

    return ToWorldHemisphere(normal, x, y, z);
}

float3 SampleCosineHemisphere(float3 normal, inout uint rngState)
{
    return SampleCosineHemisphere(normal, float2(Random01(rngState), Random01(rngState)));
}

float3 SampleGGXHalfVector(float3 normal, float roughness, const float2 sample)
{
    const float alpha = max(0.001f, roughness * roughness);
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * PI * sample.y;
    const float cosTheta = sqrt((1.0f - sample.x) /
        max(0.0001f, 1.0f + (alpha2 - 1.0f) * sample.x));
    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    return ToWorldHemisphere(normal, sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 SampleGGXHalfVector(float3 normal, float roughness, inout uint rngState)
{
    return SampleGGXHalfVector(normal, roughness, float2(Random01(rngState), Random01(rngState)));
}
//Modify End

#endif
