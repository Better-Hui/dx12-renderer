#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
//Modify Begin:2026-07-30 by BestHui
#include <Common/Noise.hlsli>
//Modify End

//Modify Begin:2026-08-10 by BestHui
Texture2D<uint4> ReSTIRGITemporalCreation : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGITemporalHit : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGITemporalLight : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRGIHistoryCreation : register(u2);
RWTexture2D<uint4> ReSTIRGIHistoryHit : register(u3);
RWTexture2D<uint4> ReSTIRGIHistoryLight : register(u4);

//Modify Begin:2026-07-30 by BestHui
static const uint ReSTIRGIMaxSpatialReuseCandidates = 17u;
//Modify End

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

//Modify Begin:2026-07-30 by BestHui
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
//Modify Begin:2026-07-30 by BestHui
        bool creationVisibilityKnown = false;
//Modify End
#if RESTIR_GI_USE_UNBIASED_SPATIAL_REUSE
        float3 candidateCreationPositions[ReSTIRGIMaxSpatialReuseCandidates];
        float3 candidateCreationNormals[ReSTIRGIMaxSpatialReuseCandidates];
        uint candidateMs[ReSTIRGIMaxSpatialReuseCandidates];
        uint candidateCount = 0u;
#endif

        if (ReSTIRGIHasCandidates(temporal))
        {
            const float temporalTarget = max(0.0f, ReSTIRGI_Luminance(
                ReSTIRGI_EvaluateContribution(surface, temporal)));
//Modify Begin:2026-07-30 by BestHui
            const bool selectedTemporal = ReSTIRGIUpdateReservoir(
                result,
                temporal,
                temporal.AverageWeight * float(temporal.M) * temporalTarget,
                FrameworkInterleavedGradientNoise2D(pixel, ReSTIRGI_FrameIndex, 0x47524933u).x,
                weightSum);
            creationVisibilityKnown = selectedTemporal && ReSTIRGIHasCreationVisibility(temporal);
//Modify End
#if RESTIR_GI_USE_UNBIASED_SPATIAL_REUSE
            candidateCreationPositions[candidateCount] = temporal.CreationPosition;
            candidateCreationNormals[candidateCount] = temporal.CreationNormal;
            candidateMs[candidateCount] = temporal.M;
            ++candidateCount;
#endif
        }

        const uint offsetStart = uint(FrameworkInterleavedGradientNoise2D(
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
                float target = max(0.0f, ReSTIRGI_Luminance(
                    ReSTIRGI_EvaluateContribution(surface, neighbor)));
                target *= ReSTIRGIComputeJacobian(
                    neighbor,
                    surface.PositionWs,
                    surface.NormalWs,
                    ReSTIRGI_MaxJacobian);

//Modify Begin:2026-07-30 by BestHui
                if (ReSTIRGIUpdateReservoir(
                    result,
                    neighbor,
                    neighbor.AverageWeight * float(neighbor.M) * target,
                    FrameworkInterleavedGradientNoise2D(
                        pixel,
                        ReSTIRGI_FrameIndex,
                        0x1ac5473du + sampleIndex * 0x9e3779b9u).x,
                    weightSum))
                {
                    creationVisibilityKnown = false;
                }
//Modify End
#if RESTIR_GI_USE_UNBIASED_SPATIAL_REUSE
                if (candidateCount < ReSTIRGIMaxSpatialReuseCandidates)
                {
                    candidateCreationPositions[candidateCount] = neighbor.CreationPosition;
                    candidateCreationNormals[candidateCount] = neighbor.CreationNormal;
                    candidateMs[candidateCount] = neighbor.M;
                    ++candidateCount;
                }
#endif
            }

        const uint unboundedM = result.M;
        result.M = min(result.M, ReSTIRGI_SpatialMaxHistoryLength);
        ReSTIRGISetCreationSurface(result, surface.PositionWs, surface.NormalWs);
//Modify Begin:2026-07-30 by BestHui
        ReSTIRGISetCreationVisibility(result, creationVisibilityKnown);
//Modify End
        const float selectedTarget = ReSTIRGIIsValid(result)
            ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, result)))
            : 0.0f;
        uint normalizationM = unboundedM;
#if RESTIR_GI_USE_UNBIASED_SPATIAL_REUSE
        if (selectedTarget > 0.0f)
        {
            normalizationM = 0u;
            [loop]
            for (uint candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex)
            {
                if (ReSTIRGI_TestVisibilityAt(
                    candidateCreationPositions[candidateIndex],
                    candidateCreationNormals[candidateIndex],
                    result))
                {
                    normalizationM += candidateMs[candidateIndex];
                }
            }
        }
#endif
        result.AverageWeight = selectedTarget > 0.0f && normalizationM > 0u
            ? min(
                weightSum / (float(normalizationM) * selectedTarget),
                ReSTIRGI_MaxSpatialWeight)
            : 0.0f;
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
