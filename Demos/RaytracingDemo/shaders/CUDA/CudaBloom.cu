#include <cuda_runtime.h>

__device__ float3 LoadRgb(cudaTextureObject_t texture, unsigned int x, unsigned int y)
{
    const float4 value = tex2D<float4>(texture, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
    return make_float3(value.x, value.y, value.z);
}

__device__ void StoreRgb(cudaSurfaceObject_t surface, unsigned int x, unsigned int y, float3 color)
{
    color.x = fminf(fmaxf(color.x, 0.0f), 1.0f);
    color.y = fminf(fmaxf(color.y, 0.0f), 1.0f);
    color.z = fminf(fmaxf(color.z, 0.0f), 1.0f);
    const uchar4 value = make_uchar4(
        static_cast<unsigned char>(color.x * 255.0f + 0.5f),
        static_cast<unsigned char>(color.y * 255.0f + 0.5f),
        static_cast<unsigned char>(color.z * 255.0f + 0.5f),
        255u);
    surf2Dwrite(value, surface, x * sizeof(uchar4), y);
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

extern "C" __global__
void PrefilterDownsampleKernel(
    cudaTextureObject_t input,
    float4* output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight,
    unsigned int outputPitchElements,
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

    const int sourceX = static_cast<int>(x * 2u + 1u);
    const int sourceY = static_cast<int>(y * 2u + 1u);
    const int offsets[10] = { 0, 0, -1, -1, -1, 1, 1, -1, 1, 1 };

    float3 total = make_float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
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
    StoreFloatRgb(output, x, y, outputPitchElements, total);
}

//Modify Begin:2026-07-28 by BestHui
extern "C" __global__
void PrefilterDownsamplePyramidKernel(
    cudaTextureObject_t input,
    float4* level0,
    float4* level1,
    float4* level2,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int level0Width,
    unsigned int level0Height,
    unsigned int level1Width,
    unsigned int level1Height,
    unsigned int level2Width,
    unsigned int level2Height,
    float threshold,
    float softThreshold,
    float intensity)
{
    extern __shared__ float4 sharedStorage[];
    float4* sharedLevel0 = sharedStorage;
    float4* sharedLevel1 = sharedStorage + 16u * 16u;

    const unsigned int tx = threadIdx.x;
    const unsigned int ty = threadIdx.y;
    const unsigned int level0X = blockIdx.x * 16u + tx;
    const unsigned int level0Y = blockIdx.y * 16u + ty;
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
        StoreFloatRgb(level0, level0X, level0Y, level0Width, total);
    }

    sharedLevel0[ty * 16u + tx] = make_float4(total.x, total.y, total.z, 1.0f);
    __syncthreads();

    if (tx < 8u && ty < 8u)
    {
        const unsigned int level1X = blockIdx.x * 8u + tx;
        const unsigned int level1Y = blockIdx.y * 8u + ty;
        const float4 c00 = sharedLevel0[(ty * 2u + 0u) * 16u + tx * 2u + 0u];
        const float4 c10 = sharedLevel0[(ty * 2u + 0u) * 16u + tx * 2u + 1u];
        const float4 c01 = sharedLevel0[(ty * 2u + 1u) * 16u + tx * 2u + 0u];
        const float4 c11 = sharedLevel0[(ty * 2u + 1u) * 16u + tx * 2u + 1u];
        const float4 averaged = make_float4(
            (c00.x + c10.x + c01.x + c11.x) * 0.25f,
            (c00.y + c10.y + c01.y + c11.y) * 0.25f,
            (c00.z + c10.z + c01.z + c11.z) * 0.25f,
            1.0f);
        sharedLevel1[ty * 8u + tx] = averaged;
        if (level1 != nullptr && level1X < level1Width && level1Y < level1Height)
        {
            level1[level1Y * level1Width + level1X] = averaged;
        }
    }
    __syncthreads();

    if (level2 != nullptr && tx < 4u && ty < 4u)
    {
        const unsigned int level2X = blockIdx.x * 4u + tx;
        const unsigned int level2Y = blockIdx.y * 4u + ty;
        if (level2X < level2Width && level2Y < level2Height)
        {
            const float4 c00 = sharedLevel1[(ty * 2u + 0u) * 8u + tx * 2u + 0u];
            const float4 c10 = sharedLevel1[(ty * 2u + 0u) * 8u + tx * 2u + 1u];
            const float4 c01 = sharedLevel1[(ty * 2u + 1u) * 8u + tx * 2u + 0u];
            const float4 c11 = sharedLevel1[(ty * 2u + 1u) * 8u + tx * 2u + 1u];
            level2[level2Y * level2Width + level2X] = make_float4(
                (c00.x + c10.x + c01.x + c11.x) * 0.25f,
                (c00.y + c10.y + c01.y + c11.y) * 0.25f,
                (c00.z + c10.z + c01.z + c11.z) * 0.25f,
                1.0f);
        }
    }
}
//Modify End

extern "C" __global__
void DownsampleKernel(
    const float4* source,
    float4* output,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int sourcePitchElements,
    unsigned int outputWidth,
    unsigned int outputHeight,
    unsigned int outputPitchElements)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= outputWidth || y >= outputHeight)
    {
        return;
    }

    const int sourceX = static_cast<int>(x * 2u);
    const int sourceY = static_cast<int>(y * 2u);
    const float3 c00 = SampleFloatNearest(source, sourceX, sourceY, sourceWidth, sourceHeight, sourcePitchElements);
    const float3 c10 = SampleFloatNearest(source, sourceX + 1, sourceY, sourceWidth, sourceHeight, sourcePitchElements);
    const float3 c01 = SampleFloatNearest(source, sourceX, sourceY + 1, sourceWidth, sourceHeight, sourcePitchElements);
    const float3 c11 = SampleFloatNearest(source, sourceX + 1, sourceY + 1, sourceWidth, sourceHeight, sourcePitchElements);
    StoreFloatRgb(output, x, y, outputPitchElements, make_float3(
        (c00.x + c10.x + c01.x + c11.x) * 0.25f,
        (c00.y + c10.y + c01.y + c11.y) * 0.25f,
        (c00.z + c10.z + c01.z + c11.z) * 0.25f));
}

extern "C" __global__
void UpsampleAddKernel(
    const float4* low,
    float4* high,
    unsigned int lowWidth,
    unsigned int lowHeight,
    unsigned int lowPitchElements,
    unsigned int highWidth,
    unsigned int highHeight,
    unsigned int highPitchElements)
{
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= highWidth || y >= highHeight)
    {
        return;
    }

    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(highWidth);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(highHeight);
    const float3 base = LoadFloatRgb(high, x, y, highPitchElements);
    const float3 bloom = SampleFloatBilinear(low, u, v, lowWidth, lowHeight, lowPitchElements);
    StoreFloatRgb(high, x, y, highPitchElements, make_float3(base.x + bloom.x, base.y + bloom.y, base.z + bloom.z));
}

extern "C" __global__
void CompositeBloomKernel(
    cudaTextureObject_t input,
    const float4* bloom,
    cudaSurfaceObject_t output,
    unsigned int width,
    unsigned int height,
    unsigned int bloomWidth,
    unsigned int bloomHeight,
    unsigned int bloomPitchBytes)
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
    const float3 glow = SampleFloatBilinear(bloom, u, v, bloomWidth, bloomHeight, bloomPitchBytes);
    StoreRgb(output, x, y, make_float3(base.x + glow.x, base.y + glow.y, base.z + glow.z));
}
