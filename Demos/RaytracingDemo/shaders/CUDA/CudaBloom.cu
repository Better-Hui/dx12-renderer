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

extern "C" __global__
void UpsampleAddKernel(
//Modify Begin:2026-07-30 by BestHui
    cudaTextureObject_t low,
    cudaTextureObject_t high,
    cudaSurfaceObject_t highOutput,
//Modify End
    unsigned int highWidth,
    unsigned int highHeight)
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
    StoreOptional(highOutput, highWidth, highHeight, x, y, make_float3(base.x + bloom.x, base.y + bloom.y, base.z + bloom.z));
//Modify End
}

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
