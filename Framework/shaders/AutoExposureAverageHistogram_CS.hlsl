//Modify Begin:2026-08-23 by Hui
RWByteAddressBuffer Histogram;
RWTexture2D<float> AdaptedLuminance;

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
};

static const uint HistogramBinCount = 256u;
groupshared float WeightedBins[HistogramBinCount];

[numthreads(HistogramBinCount, 1, 1)]
void main(uint localIndex : SV_GroupIndex)
{
    const uint count = Histogram.Load(localIndex * 4u);
    WeightedBins[localIndex] = (float)count * (float)localIndex;
    GroupMemoryBarrierWithGroupSync();

    Histogram.Store(localIndex * 4u, 0u);

    [unroll]
    for (uint step = HistogramBinCount >> 1u; step > 0u; step >>= 1u)
    {
        if (localIndex < step)
        {
            WeightedBins[localIndex] += WeightedBins[localIndex + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (localIndex == 0u)
    {
        const float totalPixels = (float)InputWidth * (float)InputHeight;
        const float blackPixels = (float)count;
        const float nonBlackPixels = max(totalPixels - blackPixels, 1.0f);
        const float weightedLogAverage = (WeightedBins[0] / nonBlackPixels) - 1.0f;
        const float weightedAverageLuminance = exp2(
            ((weightedLogAverage / (HistogramBinCount - 2u)) * LogLuminanceRange) + MinLogLuminance);

        const float previousLuminance = AdaptedLuminance[uint2(0u, 0u)];
        const float timeCoefficient = saturate(1.0f - exp(-DeltaTime * Tau));
        AdaptedLuminance[uint2(0u, 0u)] = previousLuminance +
            (weightedAverageLuminance - previousLuminance) * timeCoefficient;
    }
}
//Modify End
