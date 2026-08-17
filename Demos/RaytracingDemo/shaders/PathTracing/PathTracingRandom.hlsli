#ifndef RAYTRACING_DEMO_PATH_TRACING_RANDOM_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_RANDOM_HLSLI

#include "../Common/PathTracingConstants.hlsli"
//Modify Begin:2026-08-06 by Hui
#include <Common/Noise.hlsli>
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
//Modify Begin:2026-07-30 by Hui
    state = state * 747796405u + 2891336453u;
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
//Modify Begin:2026-08-06 by Hui
    return FrameworkInterleavedGradientNoise(pixel);
//Modify End
}

float AnimatedInterleavedGradientNoise(uint2 pixel, uint frameIndex)
{
//Modify Begin:2026-08-06 by Hui
    return FrameworkAnimatedInterleavedGradientNoise(pixel, frameIndex);
//Modify End
}

uint InitializeRandomState(uint2 pixel, uint width, uint frameIndex, uint salt)
{
//Modify Begin:2026-07-30 by Hui
    const uint pixelIndex = pixel.x + pixel.y * width;
    const uint xHash = Hash(pixel.x * 0x8da6b343u);
    const uint yHash = Hash(pixel.y * 0xd8163841u);
    const uint frameHash = Hash(frameIndex * 0xcb1ab31fu);
//Modify Begin:2026-08-06 by Hui
    const float2 noise = FrameworkInterleavedGradientNoise2D(pixel, frameIndex, salt);
    return Hash(xHash ^ yHash ^ Hash(pixelIndex) ^ frameHash ^ salt ^ asuint(noise.x) ^ asuint(noise.y));
//Modify End
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
