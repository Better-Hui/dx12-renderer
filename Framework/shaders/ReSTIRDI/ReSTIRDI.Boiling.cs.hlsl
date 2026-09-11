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
    // Use a fixed 64-lane reduction instead of wave operations inside a
    // divergent branch. The latter can produce an undefined active-lane mask
    // on hardware with a wave size different from the thread-group size.
    ReSTIRDIBoilingWeightSums[groupIndex] = reservoir.WeightSum;
    ReSTIRDIBoilingValidCounts[groupIndex] = reservoir.WeightSum > 0.0f ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (groupIndex < stride)
        {
            ReSTIRDIBoilingWeightSums[groupIndex] += ReSTIRDIBoilingWeightSums[groupIndex + stride];
            ReSTIRDIBoilingValidCounts[groupIndex] += ReSTIRDIBoilingValidCounts[groupIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    const float thresholdMultiplier = 10.0f / clamp(ReSTIRDI_BoilingFilterStrength, 0.000001f, 1.0f) - 9.0f;
    const float averageWeight = ReSTIRDIBoilingValidCounts[0] > 0u
        ? ReSTIRDIBoilingWeightSums[0] / float(ReSTIRDIBoilingValidCounts[0])
        : 0.0f;
    const float boilingThreshold = averageWeight * thresholdMultiplier;
    if (boilingThreshold > 0.0f && reservoir.WeightSum > boilingThreshold)
    {
        // Keep the selected sample and cap only the outlier weight. Clearing the
        // reservoir removes all direct lighting for the pixel; on glossy/metal
        // surfaces that makes the material look diffuse because only indirect
        // lighting remains. Boiling is a heuristic firefly filter, so preserving
        // the sample gives a stable, visually meaningful result.
        reservoir.WeightSum = boilingThreshold;
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
