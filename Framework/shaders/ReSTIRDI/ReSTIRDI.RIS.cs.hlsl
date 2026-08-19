//Modify Begin:2026-08-06 by Hui
#include "ReSTIRDISceneContract.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

RWTexture2D<uint4> ReSTIRDIRISReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDIRISReservoirState : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRDI_ScreenWidth || pixel.y >= ReSTIRDI_ScreenHeight)
    {
        return;
    }

    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    const ReSTIRDI_Surface surface = ReSTIRDI_LoadSurface(pixel);
    const uint lightCount = ReSTIRDI_GetLightCount();
    if (surface.Valid && lightCount != 0u)
    {
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < ReSTIRDI_CandidateCount; ++candidateIndex)
        {
            const uint candidateSalt = 0x4d3a2b1cu + candidateIndex * 0x9e3779b9u;
            const uint frameSalt = ReSTIRDI_FrameIndex * 0x9e3779b9u;
            const float2 lightSelectionNoise = FrameworkNoiseHash02(
                pixel ^ uint2(frameSalt, frameSalt * 0x85ebca6bu),
                candidateSalt);
            const float2 sampleNoise = FrameworkNoiseHash02(
                pixel ^ uint2(frameSalt, frameSalt * 0xc2b2ae35u),
                candidateSalt ^ 0x68bc21ebu);
            const float reservoirRandom = FrameworkNoiseHash02(
                pixel ^ uint2(frameSalt, frameSalt * 0xd1b54a35u),
                candidateSalt ^ 0x02e5be93u).x;
            const float lightSelectionRandom = (lightSelectionNoise.x + float(candidateIndex)) /
                float(ReSTIRDI_CandidateCount);
            uint lightIndex;
            float inverseSourcePdf;
            if (!ReSTIRDI_SampleLightIndex(lightSelectionRandom, lightIndex, inverseSourcePdf))
            {
                continue;
            }
            const float2 sampleUv = sampleNoise;
            const ReSTIRDI_LightSample sample = ReSTIRDI_SampleLight(lightIndex, surface, sampleUv);
            const float targetPdf = sample.Valid ? max(0.0f, ReSTIRDI_Luminance(sample.UnshadowedContribution)) : 0.0f;
            ReSTIRDIStreamSample(reservoir, lightIndex, sampleUv, targetPdf, inverseSourcePdf, reservoirRandom);
        }

        ReSTIRDIFinalizeResampling(reservoir, 1.0f, float(ReSTIRDI_CandidateCount));
        reservoir.M = 1.0f;

#if RESTIR_DI_USE_INITIAL_VISIBILITY
        if (ReSTIRDIIsValid(reservoir))
        {
            const ReSTIRDI_LightSample selectedSample = ReSTIRDI_SampleLight(
                ReSTIRDIGetLightIndex(reservoir), surface, ReSTIRDIGetSampleUv(reservoir));
            if (!ReSTIRDI_TestVisibility(surface, selectedSample))
            {
                ReSTIRDIStoreVisibility(reservoir, 0.0f, true);
            }
        }
#endif
    }

    ReSTIRDIRISReservoir[pixel] = ReSTIRDIPackReservoirCore(reservoir);
    ReSTIRDIRISReservoirState[pixel] = ReSTIRDIPackReservoirState(reservoir);
}
//Modify End
