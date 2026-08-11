#include "ReSTIRGI/ReSTIRGISceneContract.hlsli"
#include "ReSTIRGI/ReSTIRGI.hlsli"
#include "ReSTIRGI/ReSTIRGIConstants.hlsli"
//Modify Begin:2026-07-30 by BestHui
#include <Common/Noise.hlsli>
//Modify End

//Modify Begin:2026-08-10 by BestHui
RWTexture2D<uint4> ReSTIRGIInitialCreation : register(u2);
RWTexture2D<uint4> ReSTIRGIInitialHit : register(u3);
RWTexture2D<uint4> ReSTIRGIInitialLight : register(u4);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRGI_Width || pixel.y >= ReSTIRGI_Height)
    {
        return;
    }

    ReSTIRGIReservoir reservoir = ReSTIRGIEmptyReservoir();
    const ReSTIRGI_Surface surface = ReSTIRGI_LoadSurface(pixel);
    if (surface.Valid)
    {
//Modify Begin:2026-07-30 by BestHui
        float weightSum = 0.0f;
        const uint candidateCount = max(1u, ReSTIRGI_InitialCandidateCount);
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex)
        {
            const uint candidateSalt = 0x47524931u + candidateIndex * 0x9e3779b9u;
            const float lobeSelection = FrameworkInterleavedGradientNoise2D(
                pixel,
                ReSTIRGI_FrameIndex,
                candidateSalt).x;
            const float2 directionalSample = FrameworkInterleavedGradientNoise2D(
                pixel,
                ReSTIRGI_FrameIndex,
                candidateSalt ^ 0x68bc21ebu);
            const float reservoirRandom = FrameworkInterleavedGradientNoise2D(
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
                lobeSelection,
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
//Modify End
    }

    ReSTIRGIWriteReservoir(
        ReSTIRGIInitialCreation,
        ReSTIRGIInitialHit,
        ReSTIRGIInitialLight,
        pixel,
        reservoir);
}
//Modify End
