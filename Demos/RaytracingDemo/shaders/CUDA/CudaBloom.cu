#include <cuda_runtime.h>

//Modify Begin:2026-08-17 by Hui
__device__ float3 LoadPointSampleRgb(cudaTextureObject_t texture, unsigned int x, unsigned int y)
{
    const float4 value = tex2D<float4>(texture, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
    return make_float3(value.x, value.y, value.z);
}

__device__ float3 SampleNormalizedLinearRgb(cudaTextureObject_t texture, float u, float v)
{
    const float4 value = tex2D<float4>(texture, u, v);
    return make_float3(value.x, value.y, value.z);
}

__device__ float ComputeGaussianMipBlendWeight(float sigma, unsigned int level)
{
    constexpr float pi = 3.14159265358979323846f;
    const float sigmaSquared = fmaxf(sigma * sigma, 1.0e-6f);
    const float c = 2.0f * pi * sigmaSquared;
    const float fourToLevel = exp2f(2.0f * static_cast<float>(level));
    const float sixteenToLevel = fourToLevel * fourToLevel;
    return fminf(fmaxf((sixteenToLevel * logf(4.0f)) / (c * (fourToLevel + c)), 0.0f), 1.0f);
}

__device__ void StoreBloomRgb(cudaSurfaceObject_t surface, unsigned int x, unsigned int y, float3 color)
{
    surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), surface, x * sizeof(float4), y);
}

__device__ float Luminance(float3 color)
{
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

__device__ float3 ApplyBloomThreshold(float3 color, float threshold, float softThreshold)
{
    const float brightness = fmaxf(color.x, fmaxf(color.y, color.z));
    const float knee = threshold * softThreshold;
    float soft = brightness - threshold + knee;
    soft = fminf(fmaxf(soft, 0.0f), 2.0f * knee);
    soft = soft * soft * (0.25f / (knee + 0.00001f));
    const float contribution = fmaxf(soft, brightness - threshold) / fmaxf(brightness, 0.00001f);
    return make_float3(color.x * contribution, color.y * contribution, color.z * contribution);
}

__device__ float3 SampleFourTapBoxFilter(
    cudaTextureObject_t source,
    float u,
    float v,
    float texelSizeX,
    float texelSizeY)
{
    const float3 topLeft = SampleNormalizedLinearRgb(source, u - texelSizeX, v - texelSizeY);
    const float3 topRight = SampleNormalizedLinearRgb(source, u + texelSizeX, v - texelSizeY);
    const float3 bottomLeft = SampleNormalizedLinearRgb(source, u - texelSizeX, v + texelSizeY);
    const float3 bottomRight = SampleNormalizedLinearRgb(source, u + texelSizeX, v + texelSizeY);
    return make_float3(
        (topLeft.x + topRight.x + bottomLeft.x + bottomRight.x) * 0.25f,
        (topLeft.y + topRight.y + bottomLeft.y + bottomRight.y) * 0.25f,
        (topLeft.z + topRight.z + bottomLeft.z + bottomRight.z) * 0.25f);
}

__device__ float3 SampleFiveTapPrefilter(
    cudaTextureObject_t source,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight,
    unsigned int x,
    unsigned int y,
    float threshold,
    float softThreshold,
    float intensity)
{
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputHeight);
    const float texelSizeX = 2.0f / static_cast<float>(sourceWidth);
    const float texelSizeY = 2.0f / static_cast<float>(sourceHeight);
    constexpr float offsets[10] = { 0.0f, 0.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };

    float3 total = make_float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    for (int sampleIndex = 0; sampleIndex < 5; ++sampleIndex)
    {
        const float3 filtered = ApplyBloomThreshold(
            SampleNormalizedLinearRgb(
                source,
                u + offsets[sampleIndex * 2] * texelSizeX,
                v + offsets[sampleIndex * 2 + 1] * texelSizeY),
            threshold,
            softThreshold);
        const float weight = 1.0f / (Luminance(filtered) + 1.0f);
        total.x += filtered.x * weight;
        total.y += filtered.y * weight;
        total.z += filtered.z * weight;
        weightSum += weight;
    }

    return make_float3(
        total.x / weightSum * intensity,
        total.y / weightSum * intensity,
        total.z / weightSum * intensity);
}

extern "C" __global__
void BloomPrefilterDownsampleKernel(
    cudaTextureObject_t source,
    cudaSurfaceObject_t output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight,
    float threshold,
    float softThreshold,
    float intensity)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= outputWidth || y >= outputHeight)
    {
        return;
    }

    StoreBloomRgb(
        output,
        x,
        y,
        SampleFiveTapPrefilter(
            source,
            sourceWidth,
            sourceHeight,
            outputWidth,
            outputHeight,
            x,
            y,
            threshold,
            softThreshold,
            intensity));
}

extern "C" __global__
void BloomFourTapDownsampleKernel(
    cudaTextureObject_t source,
    cudaSurfaceObject_t output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= outputWidth || y >= outputHeight)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputHeight);
    StoreBloomRgb(
        output,
        x,
        y,
        SampleFourTapBoxFilter(
            source,
            u,
            v,
            1.0f / static_cast<float>(sourceWidth),
            1.0f / static_cast<float>(sourceHeight)));
}

__device__ int ClampCoordinateToExtent(const int coordinate, const unsigned int extent)
{
    const int lastCoordinate = static_cast<int>(extent) - 1;
    return coordinate < 0 ? 0 : (coordinate > lastCoordinate ? lastCoordinate : coordinate);
}

__device__ float3 AverageSharedTile4x4(
    const float3* source,
    const int stride,
    const int sourceX,
    const int sourceY)
{
    float3 sum = make_float3(0.0f, 0.0f, 0.0f);
#pragma unroll
    for (int offsetY = 0; offsetY < 4; ++offsetY)
    {
#pragma unroll
        for (int offsetX = 0; offsetX < 4; ++offsetX)
        {
            const float3 value = source[(sourceY + offsetY) * stride + sourceX + offsetX];
            sum.x += value.x;
            sum.y += value.y;
            sum.z += value.z;
        }
    }
    return make_float3(sum.x * (1.0f / 16.0f), sum.y * (1.0f / 16.0f), sum.z * (1.0f / 16.0f));
}

template <int CascadeLevels>
struct SharedDownsampleCascadeLayout
{
    static_assert(CascadeLevels >= 1 && CascadeLevels <= 4, "Unsupported shared downsample cascade length.");

    static constexpr int Mip0InteriorSize = 16;
    static constexpr int Mip1InteriorSize = 8;
    static constexpr int Mip2InteriorSize = 4;
    static constexpr int Mip3InteriorSize = 2;
    static constexpr int Mip0TileSize = 14 + (1 << (CascadeLevels + 1));
    static constexpr int Mip0TileOffset = (Mip0TileSize - Mip0InteriorSize) / 2;
    static constexpr int Mip1TileSize = 6 + (1 << CascadeLevels);
    static constexpr int Mip1TileOffset = (Mip1TileSize - Mip1InteriorSize) / 2;
    static constexpr int Mip2TileSize = CascadeLevels >= 3 ? 2 + (1 << (CascadeLevels - 1)) : 0;
    static constexpr int Mip2TileOffset = CascadeLevels >= 3 ? (Mip2TileSize - Mip2InteriorSize) / 2 : 0;
    static constexpr int Mip3TileSize = 4;
    static constexpr int Mip3TileOffset = 1;
};

template <int CascadeLevels>
__device__ __forceinline__
void RunSharedDownsampleCascade(
    cudaTextureObject_t mip0Input,
    cudaSurfaceObject_t mip1Output,
    cudaSurfaceObject_t mip2Output,
    cudaSurfaceObject_t mip3Output,
    cudaSurfaceObject_t mip4Output,
    unsigned int mip0Width,
    unsigned int mip0Height,
    unsigned int mip1Width,
    unsigned int mip1Height,
    unsigned int mip2Width,
    unsigned int mip2Height,
    unsigned int mip3Width,
    unsigned int mip3Height,
    unsigned int mip4Width,
    unsigned int mip4Height,
    float3* mip0Tile,
    float3* mip1Tile,
    float3* mip2Tile,
    float3* mip3Tile)
{
    using Layout = SharedDownsampleCascadeLayout<CascadeLevels>;
    constexpr int mip0InteriorSize = Layout::Mip0InteriorSize;
    constexpr int mip1InteriorSize = Layout::Mip1InteriorSize;
    constexpr int mip2InteriorSize = Layout::Mip2InteriorSize;
    constexpr int mip0TileSize = Layout::Mip0TileSize;
    constexpr int mip0TileOffset = Layout::Mip0TileOffset;
    constexpr int mip1TileSize = Layout::Mip1TileSize;
    constexpr int mip1TileOffset = Layout::Mip1TileOffset;

    const int threadIndex = static_cast<int>(threadIdx.y) * static_cast<int>(blockDim.x) + static_cast<int>(threadIdx.x);
    const int threadCount = static_cast<int>(blockDim.x) * static_cast<int>(blockDim.y);
    const int mip0OriginX = static_cast<int>(blockIdx.x) * mip0InteriorSize;
    const int mip0OriginY = static_cast<int>(blockIdx.y) * mip0InteriorSize;
    const int mip0TileOriginX = mip0OriginX - mip0TileOffset;
    const int mip0TileOriginY = mip0OriginY - mip0TileOffset;

    for (int index = threadIndex; index < mip0TileSize * mip0TileSize; index += threadCount)
    {
        const int localX = index % mip0TileSize;
        const int localY = index / mip0TileSize;
        const int sourceX = ClampCoordinateToExtent(mip0TileOriginX + localX, mip0Width);
        const int sourceY = ClampCoordinateToExtent(mip0TileOriginY + localY, mip0Height);
        mip0Tile[index] = LoadPointSampleRgb(mip0Input, static_cast<unsigned int>(sourceX), static_cast<unsigned int>(sourceY));
    }
    __syncthreads();

    const int localThreadX = static_cast<int>(threadIdx.x);
    const int localThreadY = static_cast<int>(threadIdx.y);
    const int mip1OriginX = static_cast<int>(blockIdx.x) * mip1InteriorSize;
    const int mip1OriginY = static_cast<int>(blockIdx.y) * mip1InteriorSize;
    if constexpr (CascadeLevels == 1)
    {
        if (localThreadX < mip1InteriorSize && localThreadY < mip1InteriorSize)
        {
            const int mip1X = mip1OriginX + localThreadX;
            const int mip1Y = mip1OriginY + localThreadY;
            if (mip1X < static_cast<int>(mip1Width) && mip1Y < static_cast<int>(mip1Height))
            {
                const int mip0SourceX = mip1X * 2 - 1 - mip0TileOriginX;
                const int mip0SourceY = mip1Y * 2 - 1 - mip0TileOriginY;
                const float3 value = AverageSharedTile4x4(mip0Tile, mip0TileSize, mip0SourceX, mip0SourceY);
                StoreBloomRgb(mip1Output, static_cast<unsigned int>(mip1X), static_cast<unsigned int>(mip1Y), value);
            }
        }
    }
    else
    {
        const int mip1TileOriginX = mip1OriginX - mip1TileOffset;
        const int mip1TileOriginY = mip1OriginY - mip1TileOffset;
        for (int index = threadIndex; index < mip1TileSize * mip1TileSize; index += threadCount)
        {
            const int localX = index % mip1TileSize;
            const int localY = index / mip1TileSize;
            const int mip1X = ClampCoordinateToExtent(mip1TileOriginX + localX, mip1Width);
            const int mip1Y = ClampCoordinateToExtent(mip1TileOriginY + localY, mip1Height);
            const int mip0SourceX = mip1X * 2 - 1 - mip0TileOriginX;
            const int mip0SourceY = mip1Y * 2 - 1 - mip0TileOriginY;
            mip1Tile[index] = AverageSharedTile4x4(mip0Tile, mip0TileSize, mip0SourceX, mip0SourceY);
        }
        __syncthreads();

        const int mip1X = mip1OriginX + localThreadX;
        const int mip1Y = mip1OriginY + localThreadY;
        if (localThreadX < mip1InteriorSize && localThreadY < mip1InteriorSize &&
            mip1X < static_cast<int>(mip1Width) && mip1Y < static_cast<int>(mip1Height))
        {
            const float3 value = mip1Tile[(localThreadY + mip1TileOffset) * mip1TileSize + localThreadX + mip1TileOffset];
            StoreBloomRgb(mip1Output, static_cast<unsigned int>(mip1X), static_cast<unsigned int>(mip1Y), value);
        }

        const int mip2OriginX = static_cast<int>(blockIdx.x) * mip2InteriorSize;
        const int mip2OriginY = static_cast<int>(blockIdx.y) * mip2InteriorSize;
        if constexpr (CascadeLevels == 2)
        {
            if (localThreadX < mip2InteriorSize && localThreadY < mip2InteriorSize)
            {
                const int mip2X = mip2OriginX + localThreadX;
                const int mip2Y = mip2OriginY + localThreadY;
                if (mip2X < static_cast<int>(mip2Width) && mip2Y < static_cast<int>(mip2Height))
                {
                    const int mip1SourceX = mip2X * 2 - 1 - mip1TileOriginX;
                    const int mip1SourceY = mip2Y * 2 - 1 - mip1TileOriginY;
                    const float3 value = AverageSharedTile4x4(mip1Tile, mip1TileSize, mip1SourceX, mip1SourceY);
                    StoreBloomRgb(mip2Output, static_cast<unsigned int>(mip2X), static_cast<unsigned int>(mip2Y), value);
                }
            }
            return;
        }

        if constexpr (CascadeLevels >= 3)
        {
            constexpr int mip3InteriorSize = Layout::Mip3InteriorSize;
            constexpr int mip2TileSize = Layout::Mip2TileSize;
            constexpr int mip2TileOffset = Layout::Mip2TileOffset;
            const int mip2TileOriginX = mip2OriginX - mip2TileOffset;
            const int mip2TileOriginY = mip2OriginY - mip2TileOffset;
            for (int index = threadIndex; index < mip2TileSize * mip2TileSize; index += threadCount)
            {
                const int localX = index % mip2TileSize;
                const int localY = index / mip2TileSize;
                const int mip2X = ClampCoordinateToExtent(mip2TileOriginX + localX, mip2Width);
                const int mip2Y = ClampCoordinateToExtent(mip2TileOriginY + localY, mip2Height);
                const int mip1SourceX = mip2X * 2 - 1 - mip1TileOriginX;
                const int mip1SourceY = mip2Y * 2 - 1 - mip1TileOriginY;
                mip2Tile[index] = AverageSharedTile4x4(mip1Tile, mip1TileSize, mip1SourceX, mip1SourceY);
            }
            __syncthreads();

            if (localThreadX < mip2InteriorSize && localThreadY < mip2InteriorSize)
            {
                const int mip2X = mip2OriginX + localThreadX;
                const int mip2Y = mip2OriginY + localThreadY;
                if (mip2X < static_cast<int>(mip2Width) && mip2Y < static_cast<int>(mip2Height))
                {
                    const float3 value = mip2Tile[(localThreadY + mip2TileOffset) * mip2TileSize + localThreadX + mip2TileOffset];
                    StoreBloomRgb(mip2Output, static_cast<unsigned int>(mip2X), static_cast<unsigned int>(mip2Y), value);
                }
            }

            const int mip3OriginX = static_cast<int>(blockIdx.x) * mip3InteriorSize;
            const int mip3OriginY = static_cast<int>(blockIdx.y) * mip3InteriorSize;
            if constexpr (CascadeLevels == 3)
            {
                if (localThreadX < mip3InteriorSize && localThreadY < mip3InteriorSize)
                {
                    const int mip3X = mip3OriginX + localThreadX;
                    const int mip3Y = mip3OriginY + localThreadY;
                    if (mip3X < static_cast<int>(mip3Width) && mip3Y < static_cast<int>(mip3Height))
                    {
                        const int mip2SourceX = mip3X * 2 - 1 - mip2TileOriginX;
                        const int mip2SourceY = mip3Y * 2 - 1 - mip2TileOriginY;
                        const float3 value = AverageSharedTile4x4(mip2Tile, mip2TileSize, mip2SourceX, mip2SourceY);
                        StoreBloomRgb(mip3Output, static_cast<unsigned int>(mip3X), static_cast<unsigned int>(mip3Y), value);
                    }
                }
            }
            if constexpr (CascadeLevels == 4)
            {
                constexpr int mip3TileSize = Layout::Mip3TileSize;
                constexpr int mip3TileOffset = Layout::Mip3TileOffset;
                const int mip3TileOriginX = mip3OriginX - mip3TileOffset;
                const int mip3TileOriginY = mip3OriginY - mip3TileOffset;
                for (int index = threadIndex; index < mip3TileSize * mip3TileSize; index += threadCount)
                {
                    const int localX = index % mip3TileSize;
                    const int localY = index / mip3TileSize;
                    const int mip3X = ClampCoordinateToExtent(mip3TileOriginX + localX, mip3Width);
                    const int mip3Y = ClampCoordinateToExtent(mip3TileOriginY + localY, mip3Height);
                    const int mip2SourceX = mip3X * 2 - 1 - mip2TileOriginX;
                    const int mip2SourceY = mip3Y * 2 - 1 - mip2TileOriginY;
                    mip3Tile[index] = AverageSharedTile4x4(mip2Tile, mip2TileSize, mip2SourceX, mip2SourceY);
                }
                __syncthreads();

                if (localThreadX < mip3InteriorSize && localThreadY < mip3InteriorSize)
                {
                    const int mip3X = mip3OriginX + localThreadX;
                    const int mip3Y = mip3OriginY + localThreadY;
                    if (mip3X < static_cast<int>(mip3Width) && mip3Y < static_cast<int>(mip3Height))
                    {
                        const float3 value = mip3Tile[(localThreadY + mip3TileOffset) * mip3TileSize + localThreadX + mip3TileOffset];
                        StoreBloomRgb(mip3Output, static_cast<unsigned int>(mip3X), static_cast<unsigned int>(mip3Y), value);
                    }
                }

                if (localThreadX == 0 && localThreadY == 0 && blockIdx.x < mip4Width && blockIdx.y < mip4Height)
                {
                    const float3 value = AverageSharedTile4x4(mip3Tile, mip3TileSize, 0, 0);
                    StoreBloomRgb(mip4Output, blockIdx.x, blockIdx.y, value);
                }
            }
        }
    }
}

// Compile-time variants keep the GPU path branch-free and right-size shared memory.
extern "C" __global__
void BloomOneLevelSharedDownsampleKernel(
    cudaTextureObject_t mip0Input,
    cudaSurfaceObject_t mip1Output,
    unsigned int mip0Width,
    unsigned int mip0Height,
    unsigned int mip1Width,
    unsigned int mip1Height)
{
    using Layout = SharedDownsampleCascadeLayout<1>;
    __shared__ float3 mip0Tile[Layout::Mip0TileSize * Layout::Mip0TileSize];
    RunSharedDownsampleCascade<1>(
        mip0Input,
        mip1Output,
        0,
        0,
        0,
        mip0Width,
        mip0Height,
        mip1Width,
        mip1Height,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        mip0Tile,
        nullptr,
        nullptr,
        nullptr);
}

extern "C" __global__
void BloomTwoLevelSharedDownsampleKernel(
    cudaTextureObject_t mip0Input,
    cudaSurfaceObject_t mip1Output,
    cudaSurfaceObject_t mip2Output,
    unsigned int mip0Width,
    unsigned int mip0Height,
    unsigned int mip1Width,
    unsigned int mip1Height,
    unsigned int mip2Width,
    unsigned int mip2Height)
{
    using Layout = SharedDownsampleCascadeLayout<2>;
    __shared__ float3 mip0Tile[Layout::Mip0TileSize * Layout::Mip0TileSize];
    __shared__ float3 mip1Tile[Layout::Mip1TileSize * Layout::Mip1TileSize];
    RunSharedDownsampleCascade<2>(
        mip0Input,
        mip1Output,
        mip2Output,
        0,
        0,
        mip0Width,
        mip0Height,
        mip1Width,
        mip1Height,
        mip2Width,
        mip2Height,
        0u,
        0u,
        0u,
        0u,
        mip0Tile,
        mip1Tile,
        nullptr,
        nullptr);
}

extern "C" __global__
void BloomThreeLevelSharedDownsampleKernel(
    cudaTextureObject_t mip0Input,
    cudaSurfaceObject_t mip1Output,
    cudaSurfaceObject_t mip2Output,
    cudaSurfaceObject_t mip3Output,
    unsigned int mip0Width,
    unsigned int mip0Height,
    unsigned int mip1Width,
    unsigned int mip1Height,
    unsigned int mip2Width,
    unsigned int mip2Height,
    unsigned int mip3Width,
    unsigned int mip3Height)
{
    using Layout = SharedDownsampleCascadeLayout<3>;
    __shared__ float3 mip0Tile[Layout::Mip0TileSize * Layout::Mip0TileSize];
    __shared__ float3 mip1Tile[Layout::Mip1TileSize * Layout::Mip1TileSize];
    __shared__ float3 mip2Tile[Layout::Mip2TileSize * Layout::Mip2TileSize];
    RunSharedDownsampleCascade<3>(
        mip0Input,
        mip1Output,
        mip2Output,
        mip3Output,
        0,
        mip0Width,
        mip0Height,
        mip1Width,
        mip1Height,
        mip2Width,
        mip2Height,
        mip3Width,
        mip3Height,
        0u,
        0u,
        mip0Tile,
        mip1Tile,
        mip2Tile,
        nullptr);
}

extern "C" __global__
void BloomFourLevelSharedDownsampleKernel(
    cudaTextureObject_t mip0Input,
    cudaSurfaceObject_t mip1Output,
    cudaSurfaceObject_t mip2Output,
    cudaSurfaceObject_t mip3Output,
    cudaSurfaceObject_t mip4Output,
    unsigned int mip0Width,
    unsigned int mip0Height,
    unsigned int mip1Width,
    unsigned int mip1Height,
    unsigned int mip2Width,
    unsigned int mip2Height,
    unsigned int mip3Width,
    unsigned int mip3Height,
    unsigned int mip4Width,
    unsigned int mip4Height)
{
    using Layout = SharedDownsampleCascadeLayout<4>;
    __shared__ float3 mip0Tile[Layout::Mip0TileSize * Layout::Mip0TileSize];
    __shared__ float3 mip1Tile[Layout::Mip1TileSize * Layout::Mip1TileSize];
    __shared__ float3 mip2Tile[Layout::Mip2TileSize * Layout::Mip2TileSize];
    __shared__ float3 mip3Tile[Layout::Mip3TileSize * Layout::Mip3TileSize];
    RunSharedDownsampleCascade<4>(
        mip0Input,
        mip1Output,
        mip2Output,
        mip3Output,
        mip4Output,
        mip0Width,
        mip0Height,
        mip1Width,
        mip1Height,
        mip2Width,
        mip2Height,
        mip3Width,
        mip3Height,
        mip4Width,
        mip4Height,
        mip0Tile,
        mip1Tile,
        mip2Tile,
        mip3Tile);
}

extern "C" __global__
void BloomAdditiveUpsampleKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int lowWidth,
    unsigned int lowHeight)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= highWidth || y >= highHeight)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(highWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(highHeight);
    const float3 base = LoadPointSampleRgb(high, x, y);
    const float3 bloom = SampleFourTapBoxFilter(
        low,
        u,
        v,
        0.5f / static_cast<float>(lowWidth),
        0.5f / static_cast<float>(lowHeight));
    StoreBloomRgb(highOutput, x, y, make_float3(
        base.x + bloom.x,
        base.y + bloom.y,
        base.z + bloom.z));
}

extern "C" __global__
void BloomCompositeKernel(
    cudaTextureObject_t input,
    cudaTextureObject_t bloom,
    cudaSurfaceObject_t output,
    unsigned int width,
    unsigned int height,
    unsigned int bloomWidth,
    unsigned int bloomHeight)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
    const float3 base = LoadPointSampleRgb(input, x, y);
    const float3 glow = SampleFourTapBoxFilter(
        bloom,
        u,
        v,
        0.5f / static_cast<float>(bloomWidth),
        0.5f / static_cast<float>(bloomHeight));
    StoreBloomRgb(output, x, y, make_float3(
        base.x + glow.x,
        base.y + glow.y,
        base.z + glow.z));
}

template <bool AddBaseMip>
__device__ void RunBoxFilterUpsample(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int lowWidth,
    unsigned int lowHeight,
    unsigned int level,
    float sigma)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= highWidth || y >= highHeight)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(highWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(highHeight);
    const float3 base = LoadPointSampleRgb(high, x, y);
    const float3 bloom = SampleFourTapBoxFilter(
        low,
        u,
        v,
        0.5f / static_cast<float>(lowWidth),
        0.5f / static_cast<float>(lowHeight));
    const float weight = ComputeGaussianMipBlendWeight(sigma, level);
    float3 fitted = make_float3(
        bloom.x + (base.x - bloom.x) * weight,
        bloom.y + (base.y - bloom.y) * weight,
        bloom.z + (base.z - bloom.z) * weight);
    if constexpr (AddBaseMip)
    {
        fitted = make_float3(fitted.x + base.x, fitted.y + base.y, fitted.z + base.z);
    }
    StoreBloomRgb(highOutput, x, y, fitted);
}

extern "C" __global__
void BloomBoxFilterAdditiveUpsampleKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int lowWidth,
    unsigned int lowHeight,
    unsigned int level,
    float sigma)
{
    RunBoxFilterUpsample<true>(
        low,
        high,
        highOutput,
        highWidth,
        highHeight,
        lowWidth,
        lowHeight,
        level,
        sigma);
}

extern "C" __global__
void BloomBoxFilterInterpolatedUpsampleKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int lowWidth,
    unsigned int lowHeight,
    unsigned int level,
    float sigma)
{
    RunBoxFilterUpsample<false>(
        low,
        high,
        highOutput,
        highWidth,
        highHeight,
        lowWidth,
        lowHeight,
        level,
        sigma);
}
//Modify End
