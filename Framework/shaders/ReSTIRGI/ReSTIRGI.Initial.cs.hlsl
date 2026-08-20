#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
#include <Common/ActivePixelList.hlsli>
//Modify Begin:2026-07-30 by Hui
#include <Common/Noise.hlsli>
//Modify End

//Modify Begin:2026-08-19 by Hui
RWTexture2D<uint4> ReSTIRGIInitialCreation : register(u2);
RWTexture2D<uint4> ReSTIRGIInitialHit : register(u3);
RWTexture2D<uint4> ReSTIRGIInitialLight : register(u4);

[numthreads(
    FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_X,
    FRAMEWORK_RAY_TRACED_PIXEL_THREAD_GROUP_SIZE_Y,
    1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel;
    if (!FrameworkResolveRayTracedPixel(
        dispatchThreadId,
        ReSTIRGI_Width,
        ReSTIRGI_Height,
        pixel))
    {
        return;
    }

    ReSTIRGIReservoir reservoir = ReSTIRGIEmptyReservoir();
    const ReSTIRGI_Surface surface = ReSTIRGI_LoadSurface(pixel);
    if (surface.Valid)
    {
        float weightSum = 0.0f;
        const uint candidateCount = max(1u, ReSTIRGI_InitialCandidateCount);
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex)
        {
            const uint candidateSalt = 0x47524931u + candidateIndex * 0x9e3779b9u;
            const float2 directionalSample = ReSTIRGI_SampleNoise(
                pixel,
                ReSTIRGI_FrameIndex,
                candidateSalt ^ 0x68bc21ebu);
            const float reservoirRandom = ReSTIRGI_SampleNoise(
                pixel,
                ReSTIRGI_FrameIndex,
                candidateSalt ^ 0x02e5be93u).x;
            uint randomState = ReSTIRGI_InitializeRandomState(
                pixel,
                ReSTIRGI_Width,
                ReSTIRGI_FrameIndex,
                candidateSalt ^ 0x7f4a7c15u);
            ReSTIRGIReservoir candidate = ReSTIRGIEmptyReservoir();
            if (!ReSTIRGI_GenerateInitialSample(
                surface,
                pixel,
                randomState,
                directionalSample,
                candidate))
            {
                continue;
            }

            const float target = ReSTIRGIIsValid(candidate)
                ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, candidate)))
                : 0.0f;
            ReSTIRGIUpdateReservoir(
                reservoir,
                candidate,
                candidate.AverageWeight * target,
                reservoirRandom,
                weightSum);
        }

        const uint candidateM = reservoir.M;
        ReSTIRGISetCreationSurface(reservoir, surface.PositionWs, surface.NormalWs);
        const float selectedTarget = ReSTIRGIIsValid(reservoir)
            ? max(0.0f, ReSTIRGI_Luminance(ReSTIRGI_EvaluateContribution(surface, reservoir)))
            : 0.0f;
        reservoir.AverageWeight = selectedTarget > 0.0f && candidateM > 0u
            ? weightSum / (float(candidateM) * selectedTarget)
            : 0.0f;
        ReSTIRGISetAge(reservoir, 0u);
    }

    ReSTIRGIWriteReservoir(
        ReSTIRGIInitialCreation,
        ReSTIRGIInitialHit,
        ReSTIRGIInitialLight,
        pixel,
        reservoir);
}
//Modify End
