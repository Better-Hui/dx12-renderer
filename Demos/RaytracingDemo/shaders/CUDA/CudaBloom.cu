#include <cuda_runtime.h>

__device__ float3 LoadRgb(cudaTextureObject_t texture, unsigned int x, unsigned int y)
{
    const float4 value = tex2D<float4>(texture, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
    return make_float3(value.x, value.y, value.z);
}

//Modify Begin:2026-07-30 by BestHui
__device__ float3 SampleLinear(cudaTextureObject_t texture, float u, float v)
{
    const float4 value = tex2D<float4>(texture, u, v);
    return make_float3(value.x, value.y, value.z);
}
//Modify End

//Modify Begin:2026-08-16 by BestHui
__device__ float MipGaussianBlendWeight(float sigma, unsigned int level)
{
    constexpr float pi = 3.14159265358979323846f;
    const float sigmaSquared = fmaxf(sigma * sigma, 1.0e-6f);
    const float c = 2.0f * pi * sigmaSquared;
    const float fourToLevel = exp2f(2.0f * static_cast<float>(level));
    const float sixteenToLevel = fourToLevel * fourToLevel;
    return fminf(fmaxf((sixteenToLevel * logf(4.0f)) / (c * (fourToLevel + c)), 0.0f), 1.0f);
}
//Modify End

__device__ void StoreRgb(cudaSurfaceObject_t surface, unsigned int x, unsigned int y, float3 color)
{
//Modify Begin:2026-07-28 by BestHui
    color.x = fminf(fmaxf(color.x, 0.0f), 250.0f);
    color.y = fminf(fmaxf(color.y, 0.0f), 250.0f);
    color.z = fminf(fmaxf(color.z, 0.0f), 250.0f);
    surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), surface, x * sizeof(float4), y);
//Modify End
}

__device__ float3 LoadFloatRgb(const float4* data, unsigned int x, unsigned int y, unsigned int pitchElements)
{
    const float4 value = data[y * pitchElements + x];
    return make_float3(value.x, value.y, value.z);
}

__device__ void StoreFloatRgb(float4* data, unsigned int x, unsigned int y, unsigned int pitchElements, float3 color)
{
    data[y * pitchElements + x] = make_float4(color.x, color.y, color.z, 1.0f);
}

__device__ float Luminance(float3 color)
{
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

__device__ float3 PrefilterColor(float3 color, float threshold, float softThreshold)
{
    const float brightness = fmaxf(color.x, fmaxf(color.y, color.z));
    const float knee = threshold * softThreshold;
    float soft = brightness - threshold + knee;
    soft = fminf(fmaxf(soft, 0.0f), 2.0f * knee);
    soft = soft * soft * (0.25f / (knee + 0.00001f));
    const float contribution = fmaxf(soft, brightness - threshold) / fmaxf(brightness, 0.00001f);
    return make_float3(color.x * contribution, color.y * contribution, color.z * contribution);
}

__device__ float3 SampleNearest(cudaTextureObject_t texture, int x, int y, unsigned int width, unsigned int height)
{
    x = min(max(x, 0), static_cast<int>(width) - 1);
    y = min(max(y, 0), static_cast<int>(height) - 1);
    return LoadRgb(texture, static_cast<unsigned int>(x), static_cast<unsigned int>(y));
}

__device__ float3 SampleFloatNearest(const float4* data, int x, int y, unsigned int width, unsigned int height, unsigned int pitchElements)
{
    x = min(max(x, 0), static_cast<int>(width) - 1);
    y = min(max(y, 0), static_cast<int>(height) - 1);
    return LoadFloatRgb(data, static_cast<unsigned int>(x), static_cast<unsigned int>(y), pitchElements);
}

__device__ float3 SampleFloatBilinear(const float4* data, float u, float v, unsigned int width, unsigned int height, unsigned int pitchElements)
{
    const float px = u * static_cast<float>(width) - 0.5f;
    const float py = v * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(floorf(px));
    const int y0 = static_cast<int>(floorf(py));
    const float tx = px - static_cast<float>(x0);
    const float ty = py - static_cast<float>(y0);

    const float3 c00 = SampleFloatNearest(data, x0, y0, width, height, pitchElements);
    const float3 c10 = SampleFloatNearest(data, x0 + 1, y0, width, height, pitchElements);
    const float3 c01 = SampleFloatNearest(data, x0, y0 + 1, width, height, pitchElements);
    const float3 c11 = SampleFloatNearest(data, x0 + 1, y0 + 1, width, height, pitchElements);

    const float3 cx0 = make_float3(
        c00.x + (c10.x - c00.x) * tx,
        c00.y + (c10.y - c00.y) * tx,
        c00.z + (c10.z - c00.z) * tx);
    const float3 cx1 = make_float3(
        c01.x + (c11.x - c01.x) * tx,
        c01.y + (c11.y - c01.y) * tx,
        c01.z + (c11.z - c01.z) * tx);
    return make_float3(
        cx0.x + (cx1.x - cx0.x) * ty,
        cx0.y + (cx1.y - cx0.y) * ty,
        cx0.z + (cx1.z - cx0.z) * ty);
}

//Modify Begin:2026-07-28 by BestHui
__device__ float3 Average4(float3 a, float3 b, float3 c, float3 d)
{
    return make_float3(
        (a.x + b.x + c.x + d.x) * 0.25f,
        (a.y + b.y + c.y + d.y) * 0.25f,
        (a.z + b.z + c.z + d.z) * 0.25f);
}

__device__ float3 ToFloat3(float4 value)
{
    return make_float3(value.x, value.y, value.z);
}

//Modify Begin:2026-07-30 by BestHui
__device__ void StoreOptional(cudaSurfaceObject_t output, unsigned int width, unsigned int height, unsigned int x, unsigned int y, float3 color)
{
    if (output != 0 && x < width && y < height)
    {
        surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), output, x * sizeof(float4), y);
    }
}
//Modify End

__device__ void GenerateBloomCascade(
    unsigned int groupX,
    unsigned int groupY,
    unsigned int threadX,
    unsigned int threadY,
    float3 level0Color,
//Modify Begin:2026-07-30 by BestHui
    cudaSurfaceObject_t level0,
    cudaSurfaceObject_t level1,
    cudaSurfaceObject_t level2,
    cudaSurfaceObject_t level3,
//Modify End
    unsigned int level0Width,
    unsigned int level0Height,
    unsigned int level1Width,
    unsigned int level1Height,
    unsigned int level2Width,
    unsigned int level2Height,
    unsigned int level3Width,
    unsigned int level3Height,
    float4* sharedLevel0)
{
    const unsigned int level0X = groupX * 8u + threadX;
    const unsigned int level0Y = groupY * 8u + threadY;

    StoreOptional(level0, level0Width, level0Height, level0X, level0Y, level0Color);
    sharedLevel0[threadY * 8u + threadX] = make_float4(level0Color.x, level0Color.y, level0Color.z, 1.0f);
    __syncthreads();

    if ((threadX % 2u) == 0u && (threadY % 2u) == 0u)
    {
        const float3 c00 = ToFloat3(sharedLevel0[(threadY + 0u) * 8u + threadX + 0u]);
        const float3 c10 = ToFloat3(sharedLevel0[(threadY + 0u) * 8u + threadX + 1u]);
        const float3 c01 = ToFloat3(sharedLevel0[(threadY + 1u) * 8u + threadX + 0u]);
        const float3 c11 = ToFloat3(sharedLevel0[(threadY + 1u) * 8u + threadX + 1u]);
        const float3 level1Color = Average4(c00, c10, c01, c11);
        const unsigned int level1X = groupX * 4u + threadX / 2u;
        const unsigned int level1Y = groupY * 4u + threadY / 2u;
        StoreOptional(level1, level1Width, level1Height, level1X, level1Y, level1Color);
        sharedLevel0[threadY * 8u + threadX] = make_float4(level1Color.x, level1Color.y, level1Color.z, 1.0f);
    }
    __syncthreads();

    if ((threadX % 4u) == 0u && (threadY % 4u) == 0u)
    {
        const float3 c00 = ToFloat3(sharedLevel0[(threadY + 0u) * 8u + threadX + 0u]);
        const float3 c10 = ToFloat3(sharedLevel0[(threadY + 0u) * 8u + threadX + 2u]);
        const float3 c01 = ToFloat3(sharedLevel0[(threadY + 2u) * 8u + threadX + 0u]);
        const float3 c11 = ToFloat3(sharedLevel0[(threadY + 2u) * 8u + threadX + 2u]);
        const float3 level2Color = Average4(c00, c10, c01, c11);
        const unsigned int level2X = groupX * 2u + threadX / 4u;
        const unsigned int level2Y = groupY * 2u + threadY / 4u;
        StoreOptional(level2, level2Width, level2Height, level2X, level2Y, level2Color);
        sharedLevel0[threadY * 8u + threadX] = make_float4(level2Color.x, level2Color.y, level2Color.z, 1.0f);
    }
    __syncthreads();

    if (threadX == 0u && threadY == 0u)
    {
        const float3 c00 = ToFloat3(sharedLevel0[0u * 8u + 0u]);
        const float3 c10 = ToFloat3(sharedLevel0[0u * 8u + 4u]);
        const float3 c01 = ToFloat3(sharedLevel0[4u * 8u + 0u]);
        const float3 c11 = ToFloat3(sharedLevel0[4u * 8u + 4u]);
        const float3 level3Color = Average4(c00, c10, c01, c11);
        StoreOptional(level3, level3Width, level3Height, groupX, groupY, level3Color);
    }
}

extern "C" __global__
void PrefilterDownsampleCascadeKernel(
    cudaTextureObject_t input,
//Modify Begin:2026-07-30 by BestHui
    cudaSurfaceObject_t level0,
    cudaSurfaceObject_t level1,
    cudaSurfaceObject_t level2,
    cudaSurfaceObject_t level3,
//Modify End
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int level0Width,
    unsigned int level0Height,
    unsigned int level1Width,
    unsigned int level1Height,
    unsigned int level2Width,
    unsigned int level2Height,
    unsigned int level3Width,
    unsigned int level3Height,
    float threshold,
    float softThreshold,
    float intensity)
{
    extern __shared__ float4 sharedStorage[];

    const unsigned int tx = threadIdx.x;
    const unsigned int ty = threadIdx.y;
    const unsigned int level0X = blockIdx.x * 8u + tx;
    const unsigned int level0Y = blockIdx.y * 8u + ty;
    const int sourceX = static_cast<int>(level0X * 2u + 1u);
    const int sourceY = static_cast<int>(level0Y * 2u + 1u);
    const int offsets[10] = { 0, 0, -1, -1, -1, 1, 1, -1, 1, 1 };

    float3 total = make_float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    if (level0X < level0Width && level0Y < level0Height)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float3 filtered = PrefilterColor(
                SampleNearest(input, sourceX + offsets[i * 2 + 0], sourceY + offsets[i * 2 + 1], sourceWidth, sourceHeight),
                threshold,
                softThreshold);
            const float weight = 1.0f / (Luminance(filtered) + 1.0f);
            total.x += filtered.x * weight;
            total.y += filtered.y * weight;
            total.z += filtered.z * weight;
            weightSum += weight;
        }
        total.x = total.x / weightSum * intensity;
        total.y = total.y / weightSum * intensity;
        total.z = total.z / weightSum * intensity;
    }

    GenerateBloomCascade(
        blockIdx.x,
        blockIdx.y,
        tx,
        ty,
        total,
        level0,
        level1,
        level2,
        level3,
        level0Width,
        level0Height,
        level1Width,
        level1Height,
        level2Width,
        level2Height,
        level3Width,
        level3Height,
        sharedStorage);
}

//Modify Begin:2026-08-16 by BestHui
template <int TapCount, bool ApplyPrefilter>
__device__ float3 SampleBloomRingDownsample(
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
    constexpr float pi = 3.14159265358979323846f;
    constexpr int ringPointCount = TapCount - 1;
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputHeight);
    const float2 texelSize = make_float2(
        1.0f / static_cast<float>(sourceWidth),
        1.0f / static_cast<float>(sourceHeight));
    const float ringStart = 2.0f / static_cast<float>(ringPointCount);
    const float sampleScale = 0.66f * 4.0f;

    if constexpr (ApplyPrefilter)
    {
        float3 total = make_float3(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        for (int i = 0; i < ringPointCount; ++i)
        {
            const float angle = (2.0f * pi / static_cast<float>(ringPointCount)) *
                (static_cast<float>(i) + ringStart);
            const float2 offset = make_float2(sinf(angle), cosf(angle));
            float3 sample = PrefilterColor(
                SampleLinear(
                    source,
                    u + sampleScale * texelSize.x * offset.x,
                    v + sampleScale * texelSize.y * offset.y),
                threshold,
                softThreshold);
            const float weight = 1.0f / (Luminance(sample) + 1.0f);
            total.x += sample.x * weight;
            total.y += sample.y * weight;
            total.z += sample.z * weight;
            weightSum += weight;
        }

        float3 center = PrefilterColor(SampleLinear(source, u, v), threshold, softThreshold);
        const float weight = 1.0f / (Luminance(center) + 1.0f);
        total.x += center.x * weight;
        total.y += center.y * weight;
        total.z += center.z * weight;
        weightSum += weight;
        return make_float3(
            total.x / weightSum * intensity,
            total.y / weightSum * intensity,
            total.z / weightSum * intensity);
    }

    float3 total = make_float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < ringPointCount; ++i)
    {
        const float angle = (2.0f * pi / static_cast<float>(ringPointCount)) *
            (static_cast<float>(i) + ringStart);
        const float2 offset = make_float2(sinf(angle), cosf(angle));
        const float3 sample = SampleLinear(
            source,
            u + sampleScale * texelSize.x * offset.x,
            v + sampleScale * texelSize.y * offset.y);
        total.x += sample.x;
        total.y += sample.y;
        total.z += sample.z;
    }

    const float3 center = SampleLinear(source, u, v);
    total.x += center.x;
    total.y += center.y;
    total.z += center.z;
    return make_float3(
        total.x / static_cast<float>(TapCount),
        total.y / static_cast<float>(TapCount),
        total.z / static_cast<float>(TapCount));
}

template <bool ApplyPrefilter>
__device__ float3 SampleBloomDiagonal5Downsample(
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
    const float2 texelSize = make_float2(
        1.0f / static_cast<float>(sourceWidth),
        1.0f / static_cast<float>(sourceHeight));
    const float offsets[10] = { 0.0f, 0.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };

    if constexpr (ApplyPrefilter)
    {
        float3 total = make_float3(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float3 sample = PrefilterColor(
                SampleLinear(
                    source,
                    u + offsets[i * 2 + 0] * texelSize.x,
                    v + offsets[i * 2 + 1] * texelSize.y),
                threshold,
                softThreshold);
            const float weight = 1.0f / (Luminance(sample) + 1.0f);
            total.x += sample.x * weight;
            total.y += sample.y * weight;
            total.z += sample.z * weight;
            weightSum += weight;
        }

        return make_float3(
            total.x / weightSum * intensity,
            total.y / weightSum * intensity,
            total.z / weightSum * intensity);
    }

    float3 total = make_float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5; ++i)
    {
        const float3 sample = SampleLinear(
            source,
            u + offsets[i * 2 + 0] * texelSize.x,
            v + offsets[i * 2 + 1] * texelSize.y);
        total.x += sample.x;
        total.y += sample.y;
        total.z += sample.z;
    }

    return make_float3(total.x * 0.2f, total.y * 0.2f, total.z * 0.2f);
}

template <int TapCount, bool ApplyPrefilter>
__device__ float3 SampleBloomDownsample(
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
    if constexpr (TapCount == 5)
    {
        return SampleBloomDiagonal5Downsample<ApplyPrefilter>(
            source,
            sourceWidth,
            sourceHeight,
            outputWidth,
            outputHeight,
            x,
            y,
            threshold,
            softThreshold,
            intensity);
    }

    return SampleBloomRingDownsample<TapCount, ApplyPrefilter>(
        source,
        sourceWidth,
        sourceHeight,
        outputWidth,
        outputHeight,
        x,
        y,
        threshold,
        softThreshold,
        intensity);
}

__device__ float3 AverageShared2x2(
    const float4* sharedStorage,
    unsigned int tileWidth,
    unsigned int x,
    unsigned int y)
{
    const float3 a = ToFloat3(sharedStorage[(y + 0u) * tileWidth + x + 0u]);
    const float3 b = ToFloat3(sharedStorage[(y + 0u) * tileWidth + x + 1u]);
    const float3 c = ToFloat3(sharedStorage[(y + 1u) * tileWidth + x + 0u]);
    const float3 d = ToFloat3(sharedStorage[(y + 1u) * tileWidth + x + 1u]);
    return Average4(a, b, c, d);
}

template <bool ApplyPrefilter>
__device__ void DownsampleBloom5TapSharedKernelBody(
    cudaTextureObject_t source,
    cudaSurfaceObject_t output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight,
    float threshold,
    float softThreshold,
    float intensity,
    float4* sharedStorage)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    const unsigned int blockOriginX = blockIdx.x * blockDim.x;
    const unsigned int blockOriginY = blockIdx.y * blockDim.y;
    const unsigned int tileWidth = blockDim.x * 2u + 2u;
    const unsigned int tileHeight = blockDim.y * 2u + 2u;
    const int tileOriginX = static_cast<int>(blockOriginX * 2u) - 1;
    const int tileOriginY = static_cast<int>(blockOriginY * 2u) - 1;

    for (unsigned int tileY = threadIdx.y; tileY < tileHeight; tileY += blockDim.y)
    {
        for (unsigned int tileX = threadIdx.x; tileX < tileWidth; tileX += blockDim.x)
        {
            const int sourceX = min(
                max(tileOriginX + static_cast<int>(tileX), 0),
                static_cast<int>(sourceWidth) - 1);
            const int sourceY = min(
                max(tileOriginY + static_cast<int>(tileY), 0),
                static_cast<int>(sourceHeight) - 1);
            const float4 value = tex2D<float4>(
                source,
                static_cast<float>(sourceX) + 0.5f,
                static_cast<float>(sourceY) + 0.5f);
            sharedStorage[tileY * tileWidth + tileX] = value;
        }
    }
    __syncthreads();

    if (x >= outputWidth || y >= outputHeight)
    {
        return;
    }

    const unsigned int localX = (x - blockOriginX) * 2u;
    const unsigned int localY = (y - blockOriginY) * 2u;
    const float3 topLeft = AverageShared2x2(sharedStorage, tileWidth, localX + 0u, localY + 0u);
    const float3 topRight = AverageShared2x2(sharedStorage, tileWidth, localX + 2u, localY + 0u);
    const float3 bottomLeft = AverageShared2x2(sharedStorage, tileWidth, localX + 0u, localY + 2u);
    const float3 bottomRight = AverageShared2x2(sharedStorage, tileWidth, localX + 2u, localY + 2u);
    const float3 center = AverageShared2x2(sharedStorage, tileWidth, localX + 1u, localY + 1u);

    if constexpr (ApplyPrefilter)
    {
        const float3 samples[5] = { topLeft, topRight, bottomLeft, bottomRight, center };
        float3 total = make_float3(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float3 filtered = PrefilterColor(samples[i], threshold, softThreshold);
            const float weight = 1.0f / (Luminance(filtered) + 1.0f);
            total.x += filtered.x * weight;
            total.y += filtered.y * weight;
            total.z += filtered.z * weight;
            weightSum += weight;
        }
        StoreOptional(
            output,
            outputWidth,
            outputHeight,
            x,
            y,
            make_float3(
                total.x / weightSum * intensity,
                total.y / weightSum * intensity,
                total.z / weightSum * intensity));
        return;
    }

    StoreOptional(
        output,
        outputWidth,
        outputHeight,
        x,
        y,
        make_float3(
            (topLeft.x + topRight.x + bottomLeft.x + bottomRight.x + center.x) * 0.2f,
            (topLeft.y + topRight.y + bottomLeft.y + bottomRight.y + center.y) * 0.2f,
            (topLeft.z + topRight.z + bottomLeft.z + bottomRight.z + center.z) * 0.2f));
}

extern "C" __global__
void PrefilterDownsample5TapSharedKernel(
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
    extern __shared__ float4 sharedStorage[];
    DownsampleBloom5TapSharedKernelBody<true>(
        source,
        output,
        sourceWidth,
        sourceHeight,
        outputWidth,
        outputHeight,
        threshold,
        softThreshold,
        intensity,
        sharedStorage);
}

extern "C" __global__
void Downsample5TapSharedKernel(
    cudaTextureObject_t source,
    cudaSurfaceObject_t output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight)
{
    extern __shared__ float4 sharedStorage[];
    DownsampleBloom5TapSharedKernelBody<false>(
        source,
        output,
        sourceWidth,
        sourceHeight,
        outputWidth,
        outputHeight,
        0.0f,
        0.0f,
        1.0f,
        sharedStorage);
}

template <int TapCount>
__device__ void DownsampleBloomKernelBody(
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

    const float3 color = SampleBloomDownsample<TapCount, false>(
        source,
        sourceWidth,
        sourceHeight,
        outputWidth,
        outputHeight,
        x,
        y,
        threshold,
        softThreshold,
        intensity);
    StoreOptional(output, outputWidth, outputHeight, x, y, color);
}

template <int TapCount>
__device__ void PrefilterBloomKernelBody(
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

    const float3 color = SampleBloomDownsample<TapCount, true>(
        source,
        sourceWidth,
        sourceHeight,
        outputWidth,
        outputHeight,
        x,
        y,
        threshold,
        softThreshold,
        intensity);
    StoreOptional(output, outputWidth, outputHeight, x, y, color);
}

#define DECLARE_BLOOM_DOWNSAMPLE_VARIANT(TapCount) \
extern "C" __global__ void PrefilterDownsample##TapCount##TapKernel( \
    cudaTextureObject_t source, cudaSurfaceObject_t output, unsigned int sourceWidth, unsigned int sourceHeight, \
    unsigned int outputWidth, unsigned int outputHeight, float threshold, float softThreshold, float intensity) \
{ PrefilterBloomKernelBody<TapCount>(source, output, sourceWidth, sourceHeight, outputWidth, outputHeight, threshold, softThreshold, intensity); } \
extern "C" __global__ void Downsample##TapCount##TapKernel( \
    cudaTextureObject_t source, cudaSurfaceObject_t output, unsigned int sourceWidth, unsigned int sourceHeight, \
    unsigned int outputWidth, unsigned int outputHeight) \
{ DownsampleBloomKernelBody<TapCount>(source, output, sourceWidth, sourceHeight, outputWidth, outputHeight, 0.0f, 0.0f, 1.0f); }

DECLARE_BLOOM_DOWNSAMPLE_VARIANT(5)
DECLARE_BLOOM_DOWNSAMPLE_VARIANT(10)
DECLARE_BLOOM_DOWNSAMPLE_VARIANT(15)

#undef DECLARE_BLOOM_DOWNSAMPLE_VARIANT
//Modify End

extern "C" __global__
void DownsampleCascadeKernel(
//Modify Begin:2026-07-30 by BestHui
    cudaTextureObject_t source,
    cudaSurfaceObject_t level0,
    cudaSurfaceObject_t level1,
    cudaSurfaceObject_t level2,
    cudaSurfaceObject_t level3,
//Modify End
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int level0Width,
    unsigned int level0Height,
    unsigned int level1Width,
    unsigned int level1Height,
    unsigned int level2Width,
    unsigned int level2Height,
    unsigned int level3Width,
    unsigned int level3Height)
{
    extern __shared__ float4 sharedStorage[];

    const unsigned int tx = threadIdx.x;
    const unsigned int ty = threadIdx.y;
    const unsigned int level0X = blockIdx.x * 8u + tx;
    const unsigned int level0Y = blockIdx.y * 8u + ty;
    const int sourceX = static_cast<int>(level0X * 2u);
    const int sourceY = static_cast<int>(level0Y * 2u);

//Modify Begin:2026-07-30 by BestHui
    const float3 c00 = SampleNearest(source, sourceX, sourceY, sourceWidth, sourceHeight);
    const float3 c10 = SampleNearest(source, sourceX + 1, sourceY, sourceWidth, sourceHeight);
    const float3 c01 = SampleNearest(source, sourceX, sourceY + 1, sourceWidth, sourceHeight);
    const float3 c11 = SampleNearest(source, sourceX + 1, sourceY + 1, sourceWidth, sourceHeight);
//Modify End
    const float3 level0Color = Average4(c00, c10, c01, c11);

    GenerateBloomCascade(
        blockIdx.x,
        blockIdx.y,
        tx,
        ty,
        level0Color,
        level0,
        level1,
        level2,
        level3,
        level0Width,
        level0Height,
        level1Width,
        level1Height,
        level2Width,
        level2Height,
        level3Width,
        level3Height,
        sharedStorage);
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
template <bool BoxFilterApproximation, bool AddBaseMip>
__device__ void UpsampleKernelBody(
//Modify Begin:2026-07-30 by BestHui
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
//Modify End
    unsigned int highWidth,
    unsigned int highHeight,
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
//Modify Begin:2026-07-30 by BestHui
    const float3 base = LoadRgb(high, x, y);
    const float3 bloom = SampleLinear(low, u, v);
//Modify End
    if constexpr (!BoxFilterApproximation)
    {
        StoreOptional(highOutput, highWidth, highHeight, x, y, make_float3(base.x + bloom.x, base.y + bloom.y, base.z + bloom.z));
        return;
    }

    const float weight = MipGaussianBlendWeight(sigma, level);
    float3 fitted = make_float3(
        bloom.x + (base.x - bloom.x) * weight,
        bloom.y + (base.y - bloom.y) * weight,
        bloom.z + (base.z - bloom.z) * weight);
    if constexpr (AddBaseMip)
    {
        fitted = make_float3(fitted.x + base.x, fitted.y + base.y, fitted.z + base.z);
    }
    StoreOptional(highOutput, highWidth, highHeight, x, y, fitted);
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
extern "C" __global__
void UpsampleClassicKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight)
{
    UpsampleKernelBody<false, false>(low, high, highOutput, highWidth, highHeight, 0u, 0.0f);
}

extern "C" __global__
void UpsampleBoxFilterKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int level,
    float sigma)
{
    UpsampleKernelBody<true, true>(low, high, highOutput, highWidth, highHeight, level, sigma);
}

extern "C" __global__
void UpsampleBoxFilterOriginalKernel(
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int level,
    float sigma)
{
    UpsampleKernelBody<true, false>(low, high, highOutput, highWidth, highHeight, level, sigma);
}
//Modify End

extern "C" __global__
void CompositeBloomKernel(
    cudaTextureObject_t input,
//Modify Begin:2026-07-30 by BestHui
    cudaTextureObject_t bloom,
//Modify End
    cudaSurfaceObject_t output,
    unsigned int width,
    unsigned int height)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
    const float3 base = LoadRgb(input, x, y);
//Modify Begin:2026-07-30 by BestHui
    const float3 glow = SampleLinear(bloom, u, v);
//Modify End
    StoreRgb(output, x, y, make_float3(base.x + glow.x, base.y + glow.y, base.z + glow.z));
}
