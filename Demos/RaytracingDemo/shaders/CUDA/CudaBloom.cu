#include <cuda_runtime.h>

//Modify Begin:2026-08-17 by Hui
__device__ float3 LoadRgb(cudaTextureObject_t texture, unsigned int x, unsigned int y)
{
    const float4 value = tex2D<float4>(texture, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
    return make_float3(value.x, value.y, value.z);
}

__device__ float3 SampleLinear(cudaTextureObject_t texture, float u, float v)
{
    const float4 value = tex2D<float4>(texture, u, v);
    return make_float3(value.x, value.y, value.z);
}

__device__ float MipGaussianBlendWeight(float sigma, unsigned int level)
{
    constexpr float pi = 3.14159265358979323846f;
    const float sigmaSquared = fmaxf(sigma * sigma, 1.0e-6f);
    const float c = 2.0f * pi * sigmaSquared;
    const float fourToLevel = exp2f(2.0f * static_cast<float>(level));
    const float sixteenToLevel = fourToLevel * fourToLevel;
    return fminf(fmaxf((sixteenToLevel * logf(4.0f)) / (c * (fourToLevel + c)), 0.0f), 1.0f);
}

__device__ void StoreRasterBloomRgb(cudaSurfaceObject_t surface, unsigned int x, unsigned int y, float3 color)
{
    surf2Dwrite(make_float4(color.x, color.y, color.z, 1.0f), surface, x * sizeof(float4), y);
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

__device__ float3 SampleRasterBloomBoxBlur(
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

__device__ float3 SampleRasterBloomQuincunxPrefilter(
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
void PrefilterDownsampleRasterBloomKernel(
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

    StoreRasterBloomRgb(
        output,
        x,
        y,
        SampleRasterBloomQuincunxPrefilter(
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
void DownsampleRasterBloomKernel(
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
    StoreRasterBloomRgb(
        output,
        x,
        y,
        SampleRasterBloomBoxBlur(
            source,
            u,
            v,
            1.0f / static_cast<float>(sourceWidth),
            1.0f / static_cast<float>(sourceHeight)));
}

extern "C" __global__
void UpsampleRasterBloomKernel(
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
    const float3 bloom = SampleRasterBloomBoxBlur(
        low,
        u,
        v,
        0.5f / static_cast<float>(lowWidth),
        0.5f / static_cast<float>(lowHeight));
    StoreRasterBloomRgb(highOutput, x, y, make_float3(
        base.x + bloom.x,
        base.y + bloom.y,
        base.z + bloom.z));
}

extern "C" __global__
void CompositeRasterBloomKernel(
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
    const float3 glow = SampleRasterBloomBoxBlur(
        bloom,
        u,
        v,
        0.5f / static_cast<float>(bloomWidth),
        0.5f / static_cast<float>(bloomHeight));
    StoreRasterBloomRgb(output, x, y, make_float3(
        base.x + glow.x,
        base.y + glow.y,
        base.z + glow.z));
}

template <bool AddBaseMip>
__device__ void UpsampleBoxFilterKernelBody(
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
    const float3 base = LoadRgb(high, x, y);
    const float3 bloom = SampleRasterBloomBoxBlur(
        low,
        u,
        v,
        0.5f / static_cast<float>(lowWidth),
        0.5f / static_cast<float>(lowHeight));
    const float weight = MipGaussianBlendWeight(sigma, level);
    float3 fitted = make_float3(
        bloom.x + (base.x - bloom.x) * weight,
        bloom.y + (base.y - bloom.y) * weight,
        bloom.z + (base.z - bloom.z) * weight);
    if constexpr (AddBaseMip)
    {
        fitted = make_float3(fitted.x + base.x, fitted.y + base.y, fitted.z + base.z);
    }
    StoreRasterBloomRgb(highOutput, x, y, fitted);
}

extern "C" __global__
void UpsampleBoxFilterKernel(
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
    UpsampleBoxFilterKernelBody<true>(
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
void UpsampleBoxFilterOriginalKernel(
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
    UpsampleBoxFilterKernelBody<false>(
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
