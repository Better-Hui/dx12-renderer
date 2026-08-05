//Modify Begin:2026-08-05 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIBoilingFilter.hlsli"

Texture2D<uint4> ReSTIRDIInputReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDIBoilingReservoir : register(u2);

cbuffer ReSTIRDIConstants : register(b1)
{
    uint ReSTIRDI_CandidateCount;
    uint ReSTIRDI_TemporalResamplingEnabled;
    uint ReSTIRDI_SpatialNeighborCount;
    uint ReSTIRDI_HistoryValid;
    uint ReSTIRDI_BoilingFilterEnabled;
    uint ReSTIRDI_VisibilityTestMask;
    uint ReSTIRDI_TemporalMaxHistoryLength;
    float ReSTIRDI_BoilingFilterStrength;
    float ReSTIRDI_SpatialSamplingRadius;
    float ReSTIRDI_TemporalNormalSimilarityThreshold;
    float ReSTIRDI_SpatialNormalSimilarityThreshold;
    float ReSTIRDI_DepthSimilarityThreshold;
    float ReSTIRDI_MaterialSimilarityThreshold;
    float ReSTIRDI_Padding0;
    float ReSTIRDI_Padding1;
};

groupshared float ReSTIRDIWaveWeights[2];
groupshared uint ReSTIRDIWaveCounts[2];

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint linearIndex = groupThreadId.x + groupThreadId.y * 8u;
    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    if (pixel.x < Camera_Width && pixel.y < Camera_Height)
    {
        reservoir = ReSTIRDIUnpackReservoir(ReSTIRDIInputReservoir.Load(int3(pixel, 0)));
    }

    const float weight = ReSTIRDIIsValid(reservoir) ? reservoir.WeightSum : 0.0f;
    const float waveWeight = WaveActiveSum(weight);
    const uint waveCount = WaveActiveCountBits(weight > 0.0f);
    const uint waveIndex = linearIndex / WaveGetLaneCount();
    if (WaveIsFirstLane())
    {
        ReSTIRDIWaveWeights[waveIndex] = waveWeight;
        ReSTIRDIWaveCounts[waveIndex] = waveCount;
    }

    GroupMemoryBarrierWithGroupSync();
    const uint activeWaveCount = (64u + WaveGetLaneCount() - 1u) / WaveGetLaneCount();
    if (linearIndex < activeWaveCount)
    {
        const float reducedWeight = WaveActiveSum(ReSTIRDIWaveWeights[linearIndex]);
        const uint reducedCount = WaveActiveSum(ReSTIRDIWaveCounts[linearIndex]);
        if (linearIndex == 0u)
        {
            ReSTIRDIWaveWeights[0] = reducedCount == 0u ? 0.0f : reducedWeight / float(reducedCount);
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (pixel.x < Camera_Width && pixel.y < Camera_Height)
    {
        if (ReSTIRDI_BoilingFilterEnabled != 0u &&
            ReSTIRDIIsBoilingOutlier(weight, ReSTIRDIWaveWeights[0], ReSTIRDI_BoilingFilterStrength))
        {
            reservoir = ReSTIRDIEmptyReservoir();
        }
        ReSTIRDIBoilingReservoir[pixel] = ReSTIRDIPackReservoir(reservoir);
    }
}
//Modify End
