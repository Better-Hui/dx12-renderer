#ifndef FRAMEWORK_COMMON_BLUE_NOISE_HLSLI
#define FRAMEWORK_COMMON_BLUE_NOISE_HLSLI

// The imported UE masks are stored as a 128x8192 2D atlas:
// 128 pixels in X, 128 pixels per frame in Y, and 64 frame slices.
static const uint FrameworkBlueNoiseTileSize = 128u;
static const uint FrameworkBlueNoiseFrameCount = 64u;

uint FrameworkBlueNoiseHash(uint value)
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

float FrameworkBlueNoiseScalar(uint2 pixel, uint frameIndex)
{
    const uint2 wrappedPixel = pixel & (FrameworkBlueNoiseTileSize - 1u);
    const uint wrappedFrame = frameIndex & (FrameworkBlueNoiseFrameCount - 1u);
    return FrameworkBlueNoiseScalarTexture.Load(int3(
        wrappedPixel.x,
        wrappedFrame * FrameworkBlueNoiseTileSize + wrappedPixel.y,
        0));
}

float2 FrameworkBlueNoiseVec2(uint2 pixel, uint frameIndex)
{
    const uint2 wrappedPixel = pixel & (FrameworkBlueNoiseTileSize - 1u);
    const uint wrappedFrame = frameIndex & (FrameworkBlueNoiseFrameCount - 1u);
    return FrameworkBlueNoiseVec2Texture.Load(int3(
        wrappedPixel.x,
        wrappedFrame * FrameworkBlueNoiseTileSize + wrappedPixel.y,
        0)).xy;
}

uint2 FrameworkBlueNoiseSaltPixelOffset(uint salt)
{
    return uint2(
        FrameworkBlueNoiseHash(salt ^ 0x68bc21ebu) & (FrameworkBlueNoiseTileSize - 1u),
        FrameworkBlueNoiseHash(salt ^ 0x02e5be93u) & (FrameworkBlueNoiseTileSize - 1u));
}

uint FrameworkBlueNoiseSaltFrameOffset(uint salt)
{
    return FrameworkBlueNoiseHash(salt ^ 0x9e3779b9u) & (FrameworkBlueNoiseFrameCount - 1u);
}

float FrameworkSampleStbnScalar(uint2 pixel, uint frameIndex, uint salt)
{
    return FrameworkBlueNoiseScalar(
        pixel + FrameworkBlueNoiseSaltPixelOffset(salt),
        frameIndex + FrameworkBlueNoiseSaltFrameOffset(salt));
}

float2 FrameworkSampleStbnVec2(uint2 pixel, uint frameIndex, uint salt)
{
    return FrameworkBlueNoiseVec2(
        pixel + FrameworkBlueNoiseSaltPixelOffset(salt),
        frameIndex + FrameworkBlueNoiseSaltFrameOffset(salt));
}

#endif
