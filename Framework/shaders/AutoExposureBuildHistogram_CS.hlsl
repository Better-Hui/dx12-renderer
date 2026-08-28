//Modify Begin:2026-08-28 by Hui
Texture2D<float4> SourceColor;
RWByteAddressBuffer Histogram;

cbuffer AutoExposureConstants
{
    uint InputWidth;
    uint InputHeight;
    uint OutputWidth;
    uint OutputHeight;
    float MinLogLuminance;
    float LogLuminanceRange;
    float DeltaTime;
    float Tau;
    float ExposureScale;
    uint ExposureEnabled;
    uint OutputMode;
    uint Padding;
};

static const uint HistogramBinCount = 256u;
static const uint ThreadsPerDimension = 16u;
static const float Epsilon = 0.0000001f;

groupshared uint GroupHistogram[HistogramBinCount];

float GetLuminance(float3 color)
{
    return dot(color, float3(0.2127f, 0.7152f, 0.0722f));
}

uint HDRToHistogramBin(float3 hdrColor)
{
    const float luminance = GetLuminance(hdrColor);
    if (luminance < Epsilon)
    {
        return 0u;
    }

    const float logLuminance = saturate(
        (log2(luminance) - MinLogLuminance) / LogLuminanceRange);
    return (uint)(logLuminance * (HistogramBinCount - 2u) + 1.0f);
}

[numthreads(ThreadsPerDimension, ThreadsPerDimension, 1)]
void main(
    uint groupIndex : SV_GroupIndex,
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    GroupHistogram[groupIndex] = 0u;
    GroupMemoryBarrierWithGroupSync();

    if (dispatchThreadId.x < InputWidth && dispatchThreadId.y < InputHeight)
    {
        const uint binIndex = HDRToHistogramBin(SourceColor[dispatchThreadId.xy].rgb);
        InterlockedAdd(GroupHistogram[binIndex], 1u);
    }

    GroupMemoryBarrierWithGroupSync();
    Histogram.InterlockedAdd(groupIndex * 4u, GroupHistogram[groupIndex]);
}
//Modify End
