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
//Modify Begin:2026-07-30 by BestHui
Texture2D<uint4> ReSTIRGIPreviousSpatialCreation : register(t15, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIPreviousSpatialHit : register(t16, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRGIPreviousSpatialLight : register(t17, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
//Modify End
Texture2D<float2> MotionVectorTexture : register(t18, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
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
bool ReSTIRGIIsHistorySpatiallyCompatible(
    const ReSTIRGI_Surface receiver,
    const ReSTIRGIReservoir history)
{
    return ReSTIRGIHasCandidates(history) &&
        ReSTIRGIGetAge(history) <= ReSTIRGI_MaxSampleAge &&
        dot(receiver.NormalWs, history.CreationNormal) >= ReSTIRGI_SpatialNormalSimilarityThreshold &&
        length(receiver.PositionWs - history.CreationPosition) <= ReSTIRGI_SpatialPositionSimilarityThreshold;
}
//Modify End

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
        ReSTIRGIReservoir previousSpatial = ReSTIRGIEmptyReservoir();
        const bool usePreviousFrameSpatial =
            ReSTIRGI_HistoryValid != 0u &&
            ReSTIRGI_TemporalResamplingEnabled != 0u &&
            ReSTIRGI_SpatialResamplingEnabled != 0u;
        if (usePreviousFrameSpatial)
        {
            const float2 motion = ReSTIRGI_LoadMotionVector(pixel) * float2(ReSTIRGI_Width, ReSTIRGI_Height);
            const int2 reprojectedPixel = int2(round(float2(pixel) + motion));
            if (all(reprojectedPixel >= 0) &&
                reprojectedPixel.x < int(ReSTIRGI_Width) &&
                reprojectedPixel.y < int(ReSTIRGI_Height))
            {
                previousSpatial = ReSTIRGIReadReservoir(
                    ReSTIRGIPreviousSpatialCreation,
                    ReSTIRGIPreviousSpatialHit,
                    ReSTIRGIPreviousSpatialLight,
                    uint2(reprojectedPixel));
                previousSpatial.M = min(previousSpatial.M, ReSTIRGI_SpatialMaxHistoryLength);
            }
        }

        float weightSum = 0.0f;
//Modify Begin:2026-07-30 by BestHui
        bool creationVisibilityKnown = false;
//Modify End
        float3 candidateCreationPositions[ReSTIRGIMaxSpatialReuseCandidates];
        float3 candidateCreationNormals[ReSTIRGIMaxSpatialReuseCandidates];
        uint candidateMs[ReSTIRGIMaxSpatialReuseCandidates];
        uint candidateCount = 0u;

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
            candidateCreationPositions[candidateCount] = temporal.CreationPosition;
            candidateCreationNormals[candidateCount] = temporal.CreationNormal;
            candidateMs[candidateCount] = temporal.M;
            ++candidateCount;
        }

        if (usePreviousFrameSpatial &&
            ReSTIRGIIsHistorySpatiallyCompatible(surface, previousSpatial))
        {
            const float jacobian = ReSTIRGIComputeJacobian(
                previousSpatial,
                surface.PositionWs,
                surface.NormalWs,
                ReSTIRGI_MaxJacobian);
            float previousTarget = max(0.0f, ReSTIRGI_Luminance(
                ReSTIRGI_EvaluateContribution(surface, previousSpatial))) * jacobian;
            if (previousTarget > 0.0f && ReSTIRGI_SpatialVisibilityEnabled != 0u &&
                !ReSTIRGI_TestVisibility(surface, previousSpatial))
            {
                previousTarget = 0.0f;
            }
//Modify Begin:2026-07-30 by BestHui
            if (ReSTIRGIUpdateReservoir(
                result,
                previousSpatial,
                previousSpatial.AverageWeight * float(previousSpatial.M) * previousTarget,
                FrameworkInterleavedGradientNoise2D(pixel, ReSTIRGI_FrameIndex, 0x4e3a1b97u).x,
                weightSum))
            {
                creationVisibilityKnown = false;
            }
//Modify End
            candidateCreationPositions[candidateCount] = previousSpatial.CreationPosition;
            candidateCreationNormals[candidateCount] = previousSpatial.CreationNormal;
            candidateMs[candidateCount] = previousSpatial.M;
            ++candidateCount;
        }

        if (ReSTIRGI_SpatialResamplingEnabled != 0u)
        {
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

                const bool usePreviousSpatial =
                    usePreviousFrameSpatial && (sampleIndex & 1u) != 0u;
                ReSTIRGIReservoir neighbor = ReSTIRGIEmptyReservoir();
                if (usePreviousSpatial)
                {
                    const uint2 neighborPixelUint = uint2(neighborPixel);
                    const float2 neighborMotion = ReSTIRGI_LoadMotionVector(neighborPixelUint) *
                        float2(ReSTIRGI_Width, ReSTIRGI_Height);
                    const int2 reprojectedNeighborPixel = int2(round(float2(neighborPixel) + neighborMotion));
                    if (all(reprojectedNeighborPixel >= 0) &&
                        reprojectedNeighborPixel.x < int(ReSTIRGI_Width) &&
                        reprojectedNeighborPixel.y < int(ReSTIRGI_Height))
                    {
                        neighbor = ReSTIRGIReadReservoir(
                            ReSTIRGIPreviousSpatialCreation,
                            ReSTIRGIPreviousSpatialHit,
                            ReSTIRGIPreviousSpatialLight,
                            uint2(reprojectedNeighborPixel));
                    }
                }
                else
                {
                    neighbor = ReSTIRGIReadReservoir(
                        ReSTIRGITemporalCreation,
                        ReSTIRGITemporalHit,
                        ReSTIRGITemporalLight,
                        uint2(neighborPixel));
                }
                if (!ReSTIRGIHasCandidates(neighbor))
                {
                    continue;
                }

                neighbor.M = min(neighbor.M, ReSTIRGI_SpatialMaxHistoryLength);
                if (usePreviousSpatial && !ReSTIRGIIsHistorySpatiallyCompatible(surface, neighbor))
                {
                    continue;
                }

                float target = max(0.0f, ReSTIRGI_Luminance(
                    ReSTIRGI_EvaluateContribution(surface, neighbor)));
                target *= ReSTIRGIComputeJacobian(
                    neighbor,
                    surface.PositionWs,
                    surface.NormalWs,
                    ReSTIRGI_MaxJacobian);
                if (target > 0.0f && ReSTIRGI_SpatialVisibilityEnabled != 0u &&
                    !ReSTIRGI_TestVisibility(surface, neighbor))
                {
                    target = 0.0f;
                }

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
                if (candidateCount < ReSTIRGIMaxSpatialReuseCandidates)
                {
                    candidateCreationPositions[candidateCount] = neighbor.CreationPosition;
                    candidateCreationNormals[candidateCount] = neighbor.CreationNormal;
                    candidateMs[candidateCount] = neighbor.M;
                    ++candidateCount;
                }
            }
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
        if (ReSTIRGI_SpatialResamplingEnabled != 0u &&
            ReSTIRGI_SpatialUnbiasedResamplingEnabled != 0u && selectedTarget > 0.0f)
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
