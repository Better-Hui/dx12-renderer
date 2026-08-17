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

//Modify Begin:2026-08-17 by BestHui
__device__ void StoreRasterMatchedRgb(cudaSurfaceObject_t surface, unsigned int x, unsigned int y, float3 color)
{
    surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), surface, x * sizeof(float4), y);
}
//Modify End

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

//Modify Begin:2026-08-17 by BestHui
__device__ float3 SampleRasterBoxBlur(
    cudaTextureObject_t source,
    float u,
    float v,
    float texelSizeX,
    float texelSizeY)
{
    const float3 topLeft = SampleLinear(source, u - texelSizeX, v - texelSizeY);
    const float3 topRight = SampleLinear(source, u + texelSizeX, v - texelSizeY);
    const float3 bottomLeft = SampleLinear(source, u - texelSizeX, v + texelSizeY);
    const float3 bottomRight = SampleLinear(source, u + texelSizeX, v + texelSizeY);
    return make_float3(
        (topLeft.x + topRight.x + bottomLeft.x + bottomRight.x) * 0.25f,
        (topLeft.y + topRight.y + bottomLeft.y + bottomRight.y) * 0.25f,
        (topLeft.z + topRight.z + bottomLeft.z + bottomRight.z) * 0.25f);
}

__device__ float3 SampleRasterCrossPrefilter(
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
        const float3 filtered = PrefilterColor(
            SampleLinear(
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
void PrefilterDownsampleRasterMatchedKernel(
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

    StoreRasterMatchedRgb(
        output,
        x,
        y,
        SampleRasterCrossPrefilter(
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
void DownsampleRasterMatchedKernel(
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
    StoreRasterMatchedRgb(
        output,
        x,
        y,
        SampleRasterBoxBlur(
            source,
            u,
            v,
            1.0f / static_cast<float>(sourceWidth),
            1.0f / static_cast<float>(sourceHeight)));
}

extern "C" __global__
void UpsampleRasterMatchedKernel(
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
    const float3 base = LoadRgb(high, x, y);
    const float3 bloom = SampleRasterBoxBlur(
        low,
        u,
        v,
        0.5f / static_cast<float>(lowWidth),
        0.5f / static_cast<float>(lowHeight));
    StoreRasterMatchedRgb(highOutput, x, y, make_float3(
        base.x + bloom.x,
        base.y + bloom.y,
        base.z + bloom.z));
}

extern "C" __global__
void CompositeRasterMatchedBloomKernel(
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
    const float3 base = LoadRgb(input, x, y);
    const float3 glow = SampleRasterBoxBlur(
        bloom,
        u,
        v,
        0.5f / static_cast<float>(bloomWidth),
        0.5f / static_cast<float>(bloomHeight));
    StoreRasterMatchedRgb(output, x, y, make_float3(
        base.x + glow.x,
        base.y + glow.y,
        base.z + glow.z));
}
//Modify End

//Modify Begin:2026-08-17 by BestHui
__device__ void StoreOptional(cudaSurfaceObject_t output, unsigned int width, unsigned int height, unsigned int x, unsigned int y, float3 color)
{
    if (output != 0 && x < width && y < height)
    {
        surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), output, x * sizeof(float4), y);
    }
}
//Modify End

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
    constexpr float sampleScale = 1.41421356237f;

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
