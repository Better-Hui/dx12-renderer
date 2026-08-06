//Modify Begin:2026-07-30 by BestHui
#include "ReSTIRDISceneContract.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

RWTexture2D<uint4> ReSTIRDIRISReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDIRISReservoirState : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    const SurfaceData surface = LoadGBufferSurface(pixel);
    const uint lightCount = GetReSTIRDILightCount();
    if (surface.Valid && lightCount != 0u)
    {
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < ReSTIRDI_CandidateCount; ++candidateIndex)
        {
//Modify Begin:2026-08-06 by BestHui
            const uint candidateSalt = 0x4d3a2b1cu + candidateIndex * 0x9e3779b9u;
            const float2 lightSelectionNoise = FrameworkInterleavedGradientNoise2D(
                pixel,
                Camera_FrameIndex,
                candidateSalt);
            const float2 sampleNoise = FrameworkInterleavedGradientNoise2D(
                pixel,
                Camera_FrameIndex,
                candidateSalt ^ 0x68bc21ebu);
            const float reservoirRandom = FrameworkInterleavedGradientNoise2D(
                pixel,
                Camera_FrameIndex,
                candidateSalt ^ 0x02e5be93u).x;
            const float lightSelectionRandom = (lightSelectionNoise.x + float(candidateIndex)) /
                float(ReSTIRDI_CandidateCount);
            const uint lightIndex = min(uint(lightSelectionRandom * float(lightCount)), lightCount - 1u);
            const float2 sampleUv = sampleNoise;
//Modify End
            const ReSTIRDIDirectLightSample sample = SampleReSTIRDIDirectLight(lightIndex, surface, sampleUv);
            const float targetPdf = sample.Valid ? max(0.0f, Luminance(sample.UnshadowedContribution)) : 0.0f;
            ReSTIRDIStreamSample(reservoir, lightIndex, sampleUv, targetPdf, float(lightCount), reservoirRandom);
        }

        ReSTIRDIFinalizeResampling(reservoir, 1.0f, float(ReSTIRDI_CandidateCount));
        reservoir.M = 1.0f;

        if (ReSTIRDI_InitialVisibilityEnabled != 0u && ReSTIRDIIsValid(reservoir))
        {
            const ReSTIRDIDirectLightSample selectedSample = SampleReSTIRDIDirectLight(
                ReSTIRDIGetLightIndex(reservoir), surface, ReSTIRDIGetSampleUv(reservoir));
            if (!IsReSTIRDIDirectLightSampleVisible(surface, selectedSample))
            {
                ReSTIRDIStoreVisibility(reservoir, 0.0f, true);
            }
        }
    }

    ReSTIRDIRISReservoir[pixel] = ReSTIRDIPackReservoirCore(reservoir);
    ReSTIRDIRISReservoirState[pixel] = ReSTIRDIPackReservoirState(reservoir);
}
//Modify End
