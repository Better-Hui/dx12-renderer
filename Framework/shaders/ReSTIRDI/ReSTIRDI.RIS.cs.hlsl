//Modify Begin:2026-08-05 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"

RWTexture2D<uint4> ReSTIRDIRISReservoir : register(u2);

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

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracingShared.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    const SurfaceData surface = LoadGBufferSurface(pixel);
    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    if (surface.Valid)
    {
        const uint totalLightCount = GetReSTIRDILightCount();
        uint rngState = InitializeRandomState(pixel, Camera_Width, Camera_FrameIndex, 0x4d3a2b1cu);
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < ReSTIRDI_CandidateCount && totalLightCount != 0u; ++candidateIndex)
        {
            const uint lightIndex = min(uint(Random01(rngState) * float(totalLightCount)), totalLightCount - 1u);
            const uint sampleSeed = rngState & 0x0000ffffu;
            const ReSTIRDIDirectLightSample sample = SampleReSTIRDIDirectLight(lightIndex, surface, sampleSeed);
            const bool visible = !ReSTIRDIShouldTestVisibility(
                ReSTIRDI_VisibilityTestMask,
                ReSTIRDIVisibilityStageCandidate) ||
                IsReSTIRDIDirectLightSampleVisible(surface, sample);
            const float targetPdf = visible ? max(0.0f, Luminance(sample.UnshadowedContribution)) : 0.0f;
            ReSTIRDIStreamSample(
                reservoir,
                lightIndex,
                sampleSeed,
                targetPdf * float(totalLightCount),
                targetPdf,
                1.0f,
                0u,
                Random01(rngState));
        }
        ReSTIRDIFinalizeResampling(reservoir);
        reservoir.M = 1.0f;
    }

    ReSTIRDIRISReservoir[pixel] = ReSTIRDIPackReservoir(reservoir);
}
//Modify End
