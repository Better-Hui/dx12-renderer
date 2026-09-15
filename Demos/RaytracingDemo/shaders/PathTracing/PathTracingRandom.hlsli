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

float Random01(inout uint state)
{
    // The first three dimensions of every pixel stream come directly from the
    // spatiotemporal blue-noise masks. The packed state uses 10 bits per
    // dimension, matching the effective precision of the imported 8-bit masks
    // while retaining the existing uint state footprint.
    const uint blueNoisePhase = state >> 30u;
    if (blueNoisePhase == 3u)
    {
        const uint x = (state >> 20u) & 0x3ffu;
        state = (state & 0x000fffffu) | 0x80000000u;
        return (float(x) + 0.5f) / 1024.0f;
    }
    if (blueNoisePhase == 2u)
    {
        const uint y = (state >> 10u) & 0x3ffu;
        state = (state & 0x000003ffu) | 0x40000000u;
        return (float(y) + 0.5f) / 1024.0f;
    }
    if (blueNoisePhase == 1u)
    {
        const uint scalar = state & 0x3ffu;
        state = Hash(scalar) & 0x3fffffffu;
        return (float(scalar) + 0.5f) / 1024.0f;
    }
//Modify Begin:2026-07-30 by Hui
    state = (state * 747796405u + 2891336453u) & 0x3fffffffu;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return (float(word) + 0.5f) / 4294967296.0f;
//Modify End
}

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
//Modify Begin:2026-08-06 by Hui
    (void)width;
    const float2 blueNoiseVec2 = FrameworkSampleStbnVec2(pixel, frameIndex, salt);
    const float blueNoiseScalar = FrameworkSampleStbnScalar(pixel, frameIndex, salt ^ 0x68bc21ebu);
    const uint blueNoiseX = min(uint(saturate(blueNoiseVec2.x) * 1023.0f), 1023u);
    const uint blueNoiseY = min(uint(saturate(blueNoiseVec2.y) * 1023.0f), 1023u);
    const uint blueNoiseZ = min(uint(saturate(blueNoiseScalar) * 1023.0f), 1023u);
    return 0xc0000000u | (blueNoiseX << 20u) | (blueNoiseY << 10u) | blueNoiseZ;
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
