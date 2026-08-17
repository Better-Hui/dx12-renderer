#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
//Modify Begin:2026-07-30 by Hui
#include <Common/Noise.hlsli>
//Modify End

//Modify Begin:2026-08-10 by Hui
Texture2D<uint4> ReSTIRGITemporalCreation : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGITemporalHit : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGITemporalLight : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRGIHistoryCreation : register(u2);
RWTexture2D<uint4> ReSTIRGIHistoryHit : register(u3);
RWTexture2D<uint4> ReSTIRGIHistoryLight : register(u4);

static const float2 ReSTIRGISpatialOffsets[16] =
{
    float2(0.125f, -0.375f), float2(-0.625f, -0.125f),
    float2(0.500f, 0.250f), float2(-0.125f, 0.750f),
    float2(0.875f, -0.625f), float2(-0.750f, 0.500f),
    float2(0.375f, 0.875f), float2(-0.375f, -0.875f),
    float2(0.625f, -0.375f), float2(-0.875f, 0.125f),
    float2(0.125f, 0.375f), float2(-0.500f, 0.875f),
    float2(0.875f, 0.625f), float2(-0.625f, -0.750f),
    float2(0.375f, -0.125f), float2(-0.125f, 0.125f)
};

bool ReSTIRGIIsSpatiallyCompatible(
    const ReSTIRGI_Surface receiver,
    const ReSTIRGI_Surface neighbor)
{
    return neighbor.Valid &&
        dot(receiver.NormalWs, neighbor.NormalWs) >= ReSTIRGI_SpatialNormalSimilarityThreshold &&
        length(receiver.PositionWs - neighbor.PositionWs) <= ReSTIRGI_SpatialPositionSimilarityThreshold;
}

//Modify Begin:2026-07-30 by Hui
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRGI_Width || pixel.y >= ReSTIRGI_Height)
    {
        return;
    }

    ReSTIRGIReservoir result = ReSTIRGIEmptyReservoir();
    const ReSTIRGI_Surface surface = ReSTIRGI_LoadSurface(pixel);
    if (surface.Valid)
    {
        const ReSTIRGIReservoir temporal = ReSTIRGIReadReservoir(
            ReSTIRGITemporalCreation,
            ReSTIRGITemporalHit,
            ReSTIRGITemporalLight,
            pixel);
        float weightSum = 0.0f;
        bool creationVisibilityKnown = false;
        float selectedTarget = 0.0f;
        int selectedNeighborIndex = -1;
        uint acceptedNeighborMask = 0u;

        if (ReSTIRGIHasCandidates(temporal))
        {
            const float temporalTarget = max(0.0f, ReSTIRGI_Luminance(
                ReSTIRGI_EvaluateContribution(surface, temporal)));
            if (ReSTIRGIUpdateReservoir(
                result,
                temporal,
                temporal.AverageWeight * float(temporal.M) * temporalTarget,
                ReSTIRGI_SampleNoise(pixel, ReSTIRGI_FrameIndex, 0x47524933u).x,
                weightSum))
            {
                creationVisibilityKnown = ReSTIRGIHasCreationVisibility(temporal);
                selectedTarget = temporalTarget;
            }
        }

        const uint offsetStart = uint(ReSTIRGI_SampleNoise(
            pixel,
            ReSTIRGI_FrameIndex,
            0x739c52d1u).x * 16.0f);
        [loop]
        for (uint sampleIndex = 0u; sampleIndex < ReSTIRGI_SpatialNeighborCount; ++sampleIndex)
        {
            const float2 offset = ReSTIRGISpatialOffsets[(offsetStart + sampleIndex) & 15u];
            const int2 neighborPixel = clamp(
                int2(pixel) + int2(round(offset * ReSTIRGI_SpatialSamplingRadius)),
                int2(0, 0),
                int2(int(ReSTIRGI_Width) - 1, int(ReSTIRGI_Height) - 1));
            const ReSTIRGI_Surface neighborSurface = ReSTIRGI_LoadSurface(uint2(neighborPixel));
            if (!ReSTIRGIIsSpatiallyCompatible(surface, neighborSurface))
            {
                continue;
            }

            ReSTIRGIReservoir neighbor = ReSTIRGIReadReservoir(
                ReSTIRGITemporalCreation,
                ReSTIRGITemporalHit,
                ReSTIRGITemporalLight,
                uint2(neighborPixel));
            if (!ReSTIRGIHasCandidates(neighbor))
            {
                continue;
            }

            neighbor.M = min(neighbor.M, ReSTIRGI_SpatialMaxHistoryLength);
            const float target = max(0.0f, ReSTIRGI_Luminance(
                ReSTIRGI_EvaluateContribution(surface, neighbor)));
            const float jacobian = ReSTIRGIComputeJacobian(
                neighbor,
                surface.PositionWs,
                surface.NormalWs,
                ReSTIRGI_MaxJacobian);
            if (jacobian <= 0.0f)
            {
                continue;
            }

            acceptedNeighborMask |= 1u << sampleIndex;
            if (ReSTIRGIUpdateReservoir(
                result,
                neighbor,
                neighbor.AverageWeight * float(neighbor.M) * target * jacobian,
                ReSTIRGI_SampleNoise(
                    pixel,
                    ReSTIRGI_FrameIndex,
                    0x1ac5473du + sampleIndex * 0x9e3779b9u).x,
                weightSum))
            {
                creationVisibilityKnown = false;
                selectedTarget = target;
                selectedNeighborIndex = int(sampleIndex);
            }
        }

        const uint unboundedM = result.M;
        result.M = min(result.M, ReSTIRGI_SpatialMaxHistoryLength);
        ReSTIRGISetCreationSurface(result, surface.PositionWs, surface.NormalWs);
        ReSTIRGISetCreationVisibility(result, creationVisibilityKnown);
        selectedTarget = ReSTIRGIIsValid(result)
            ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, result)))
            : 0.0f;

#if RESTIR_GI_USE_RAY_TRACED_SPATIAL_BIAS_CORRECTION
        if (selectedTarget > 0.0f)
        {
            float selectedSourceTarget = selectedTarget;
            float sourceTargetSum = ReSTIRGIHasCandidates(temporal)
                ? selectedTarget * float(temporal.M)
                : 0.0f;
            [loop]
            for (uint sampleIndex = 0u; sampleIndex < ReSTIRGI_SpatialNeighborCount; ++sampleIndex)
            {
                if ((acceptedNeighborMask & (1u << sampleIndex)) == 0u)
                {
                    continue;
                }

                const float2 offset = ReSTIRGISpatialOffsets[(offsetStart + sampleIndex) & 15u];
                const int2 neighborPixel = clamp(
                    int2(pixel) + int2(round(offset * ReSTIRGI_SpatialSamplingRadius)),
                    int2(0, 0),
                    int2(int(ReSTIRGI_Width) - 1, int(ReSTIRGI_Height) - 1));
                const ReSTIRGI_Surface neighborSurface = ReSTIRGI_LoadSurface(uint2(neighborPixel));
                ReSTIRGIReservoir neighbor = ReSTIRGIReadReservoir(
                    ReSTIRGITemporalCreation,
                    ReSTIRGITemporalHit,
                    ReSTIRGITemporalLight,
                    uint2(neighborPixel));
                neighbor.M = min(neighbor.M, ReSTIRGI_SpatialMaxHistoryLength);

                float sourceTarget = max(0.0f, ReSTIRGI_Luminance(
                    ReSTIRGI_EvaluateContribution(neighborSurface, result)));
                if (sourceTarget > 0.0f && !ReSTIRGI_TestVisibilityAt(
                    neighborSurface.PositionWs,
                    neighborSurface.NormalWs,
                    result))
                {
                    sourceTarget = 0.0f;
                }
                if (selectedNeighborIndex == int(sampleIndex))
                {
                    selectedSourceTarget = sourceTarget;
                }
                sourceTargetSum += sourceTarget * float(neighbor.M);
            }

            result.AverageWeight = sourceTargetSum > 0.0f
                ? min(
                    weightSum * selectedSourceTarget / (selectedTarget * sourceTargetSum),
                    ReSTIRGI_MaxSpatialWeight)
                : 0.0f;
        }
        else
        {
            result.AverageWeight = 0.0f;
        }
#else
        result.AverageWeight = selectedTarget > 0.0f && unboundedM > 0u
            ? min(
                weightSum / (float(unboundedM) * selectedTarget),
                ReSTIRGI_MaxSpatialWeight)
            : 0.0f;
#endif
        ReSTIRGISetAge(result, min(ReSTIRGIGetAge(result) + 1u, ReSTIRGI_MaxSampleAge));
    }

    ReSTIRGIWriteReservoir(
        ReSTIRGIHistoryCreation,
        ReSTIRGIHistoryHit,
        ReSTIRGIHistoryLight,
        pixel,
        result);
}
//Modify End
