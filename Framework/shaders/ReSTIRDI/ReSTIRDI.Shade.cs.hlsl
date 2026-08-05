//Modify Begin:2026-08-05 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"

Texture2D<uint4> ReSTIRDIFinalReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

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
    float3 lighting = 0.0f;
    float hitDistance = 0.0f;
    const ReSTIRDIReservoir reservoir = ReSTIRDIUnpackReservoir(ReSTIRDIFinalReservoir.Load(int3(pixel, 0)));
    if (surface.Valid && ReSTIRDIIsValid(reservoir) && reservoir.LightIndex < GetReSTIRDILightCount())
    {
        const ReSTIRDIDirectLightSample selectedSample = SampleReSTIRDIDirectLight(reservoir.LightIndex, surface, reservoir.SampleSeed);
        if (selectedSample.Valid)
        {
            const bool visible = !ReSTIRDIShouldTestVisibility(
                ReSTIRDI_VisibilityTestMask,
                ReSTIRDIVisibilityStageFinal) ||
                IsReSTIRDIDirectLightSampleVisible(surface, selectedSample);
            if (visible)
            {
                lighting = selectedSample.UnshadowedContribution * ReSTIRDIGetFinalWeight(reservoir);
                hitDistance = selectedSample.Distance;
            }
        }
    }

    DirectLighting[pixel] = float4(lighting, hitDistance);
}
//Modify End
