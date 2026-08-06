//Modify Begin:2026-07-30 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

RWTexture2D<uint4> ReSTIRDIRISReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDIRISReservoirState : register(u3);

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracingShared.hlsli"

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
        uint rngState = InitializeRandomState(pixel, Camera_Width, Camera_FrameIndex, 0x4d3a2b1cu);
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < ReSTIRDI_CandidateCount; ++candidateIndex)
        {
//Modify Begin:2026-08-06 by BestHui
            const float2 candidateNoise = FrameworkInterleavedGradientNoise2D(
                pixel,
                Camera_FrameIndex,
                0x4d3a2b1cu + candidateIndex * 0x9e3779b9u);
            const float lightSelectionRandom = (candidateNoise.x + float(candidateIndex)) /
                float(ReSTIRDI_CandidateCount);
            const uint lightIndex = min(uint(lightSelectionRandom * float(lightCount)), lightCount - 1u);
            const float2 sampleUv = candidateNoise;
//Modify End
            const ReSTIRDIDirectLightSample sample = SampleReSTIRDIDirectLight(lightIndex, surface, sampleUv);
            const float targetPdf = sample.Valid ? max(0.0f, Luminance(sample.UnshadowedContribution)) : 0.0f;
            ReSTIRDIStreamSample(reservoir, lightIndex, sampleUv, targetPdf, float(lightCount), Random01(rngState));
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
