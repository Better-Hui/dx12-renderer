//Modify Begin:2026-08-19 by Hui
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

RWTexture2D<uint4> ReSTIRDIBoilingReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDIBoilingReservoirState : register(u3);

groupshared float ReSTIRDIBoilingWeightSums[64];
groupshared uint ReSTIRDIBoilingValidCounts[64];

void ApplyReSTIRDIBoilingFilter(
    const uint groupIndex,
    inout ReSTIRDIReservoir reservoir)
{
    float waveWeightSum = WaveActiveSum(reservoir.WeightSum);
    uint waveValidCount = WaveActiveCountBits(reservoir.WeightSum > 0.0f);
    const uint waveIndex = groupIndex / WaveGetLaneCount();
    if (WaveIsFirstLane())
    {
        ReSTIRDIBoilingWeightSums[waveIndex] = waveWeightSum;
        ReSTIRDIBoilingValidCounts[waveIndex] = waveValidCount;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint waveCount = (64u + WaveGetLaneCount() - 1u) / WaveGetLaneCount();
    if (groupIndex < waveCount)
    {
        waveWeightSum = ReSTIRDIBoilingWeightSums[groupIndex];
        waveValidCount = ReSTIRDIBoilingValidCounts[groupIndex];
        waveWeightSum = WaveActiveSum(waveWeightSum);
        waveValidCount = WaveActiveSum(waveValidCount);
        if (groupIndex == 0u)
        {
            ReSTIRDIBoilingWeightSums[0] = waveValidCount > 0u
                ? waveWeightSum / float(waveValidCount)
                : 0.0f;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const float thresholdMultiplier = 10.0f / clamp(ReSTIRDI_BoilingFilterStrength, 0.000001f, 1.0f) - 9.0f;
    if (reservoir.WeightSum > ReSTIRDIBoilingWeightSums[0] * thresholdMultiplier)
    {
        reservoir = ReSTIRDIEmptyReservoir();
    }
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    const uint2 pixel = dispatchThreadId.xy;
    uint width;
    uint height;
    ReSTIRDIBoilingReservoir.GetDimensions(width, height);
    const bool pixelInBounds = pixel.x < width && pixel.y < height;
    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    if (pixelInBounds)
    {
        reservoir = ReSTIRDIUnpackReservoir(
            ReSTIRDIBoilingReservoir[pixel],
            ReSTIRDIBoilingReservoirState[pixel]);
    }

    ApplyReSTIRDIBoilingFilter(groupIndex, reservoir);
    if (pixelInBounds)
    {
        ReSTIRDIBoilingReservoir[pixel] = ReSTIRDIPackReservoirCore(reservoir);
        ReSTIRDIBoilingReservoirState[pixel] = ReSTIRDIPackReservoirState(reservoir);
    }
}
//Modify End
