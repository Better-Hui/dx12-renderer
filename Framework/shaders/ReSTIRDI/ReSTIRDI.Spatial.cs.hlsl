//Modify Begin:2026-08-05 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"

Texture2D<uint4> ReSTIRDITemporalReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDISpatialReservoir : register(u2);

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
#include "ReSTIRDI/ReSTIRDISurface.hlsli"

static const float2 ReSTIRDISpatialOffsets[16] =
{
    float2(0.0f, -0.45f), float2(0.55f, 0.15f),
    float2(-0.50f, 0.20f), float2(0.20f, 0.65f),
    float2(-0.15f, -0.80f), float2(0.80f, -0.35f),
    float2(-0.75f, -0.45f), float2(0.55f, 0.65f),
    float2(-0.60f, 0.70f), float2(0.90f, 0.20f),
    float2(-0.90f, 0.10f), float2(0.25f, -0.95f),
    float2(-0.25f, 0.35f), float2(0.10f, 0.90f),
    float2(-0.45f, -0.10f), float2(0.45f, -0.60f)
};

float ReSTIRDIGetTargetPdf(const ReSTIRDIReservoir reservoir, const SurfaceData surface)
{
    if (!ReSTIRDIIsValid(reservoir) || reservoir.LightIndex >= GetReSTIRDILightCount())
    {
        return 0.0f;
    }

    const ReSTIRDIDirectLightSample sample = SampleReSTIRDIDirectLight(
        reservoir.LightIndex,
        surface,
        reservoir.SampleSeed);
    return max(0.0f, Luminance(sample.UnshadowedContribution));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    const ReSTIRDIReservoir centerReservoir = ReSTIRDIUnpackReservoir(ReSTIRDITemporalReservoir.Load(int3(pixel, 0)));
    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    const SurfaceData surface = LoadGBufferSurface(pixel);
    const uint totalLightCount = GetReSTIRDILightCount();
    if (surface.Valid && totalLightCount != 0u)
    {
        uint rngState = InitializeRandomState(pixel, Camera_Width, Camera_FrameIndex, 0x7a3d1f0bu);
        int selectedNeighborIndex = -1;
        uint validNeighborMask = 0u;
        uint validNeighborCount = 0u;
        const uint neighborOffsetStart = uint(Random01(rngState) * 16.0f) & 15u;
        if (ReSTIRDIIsValid(centerReservoir) && centerReservoir.LightIndex < totalLightCount)
        {
            ReSTIRDICombineReservoirs(
                reservoir,
                centerReservoir,
                centerReservoir.SelectedTargetPdf,
                0.5f);
        }

        [loop]
        for (uint neighborIndex = 0u; neighborIndex < ReSTIRDI_SpatialNeighborCount; ++neighborIndex)
        {
            const uint offsetIndex = (neighborOffsetStart + neighborIndex) & 15u;
            const int2 offset = int2(ReSTIRDISpatialOffsets[offsetIndex] * ReSTIRDI_SpatialSamplingRadius);
            const int2 neighborPixel = clamp(
                int2(pixel) + offset,
                int2(0, 0),
                int2(int(Camera_Width) - 1, int(Camera_Height) - 1));
            const ReSTIRDIReservoir neighbor = ReSTIRDIUnpackReservoir(ReSTIRDITemporalReservoir.Load(int3(neighborPixel, 0)));
            const SurfaceData neighborSurface = LoadGBufferSurface(uint2(neighborPixel));
            const float receiverDepth = length(surface.PositionWs - Camera_Position.xyz);
            const float neighborDepth = length(neighborSurface.PositionWs - Camera_Position.xyz);
            const bool surfacesCompatible = ReSTIRDIHaveCompatibleSurfaces(
                surface,
                receiverDepth,
                neighborSurface,
                neighborDepth,
                ReSTIRDI_SpatialNormalSimilarityThreshold,
                ReSTIRDI_DepthSimilarityThreshold,
                ReSTIRDI_MaterialSimilarityThreshold);
            if (surfacesCompatible && ReSTIRDIIsValid(neighbor) && neighbor.LightIndex < totalLightCount)
            {
                const ReSTIRDIDirectLightSample neighborSample = SampleReSTIRDIDirectLight(
                    neighbor.LightIndex,
                    surface,
                    neighbor.SampleSeed);
                const bool visible = !ReSTIRDIShouldTestVisibility(
                    ReSTIRDI_VisibilityTestMask,
                    ReSTIRDIVisibilityStageSpatial) ||
                    IsReSTIRDIDirectLightSampleVisible(surface, neighborSample);
                if (visible)
                {
                    ++validNeighborCount;
                    validNeighborMask |= 1u << neighborIndex;
                    const float targetPdfAtCenter = max(0.0f, Luminance(neighborSample.UnshadowedContribution));
                    if (ReSTIRDICombineReservoirs(
                        reservoir,
                        neighbor,
                        targetPdfAtCenter,
                        Random01(rngState)))
                    {
                        selectedNeighborIndex = int(neighborIndex);
                    }
                }
            }
        }

        if (ReSTIRDIIsValid(reservoir))
        {
            float normalizationNumerator = reservoir.SelectedTargetPdf;
            float normalizationDenominator = reservoir.SelectedTargetPdf * centerReservoir.M;
            [loop]
            for (uint neighborIndex = 0u; neighborIndex < ReSTIRDI_SpatialNeighborCount; ++neighborIndex)
            {
                if ((validNeighborMask & (1u << neighborIndex)) == 0u)
                {
                    continue;
                }

                const uint offsetIndex = (neighborOffsetStart + neighborIndex) & 15u;
                const int2 offset = int2(ReSTIRDISpatialOffsets[offsetIndex] * ReSTIRDI_SpatialSamplingRadius);
                const int2 neighborPixel = clamp(
                    int2(pixel) + offset,
                    int2(0, 0),
                    int2(int(Camera_Width) - 1, int(Camera_Height) - 1));
                const ReSTIRDIReservoir neighbor = ReSTIRDIUnpackReservoir(ReSTIRDITemporalReservoir.Load(int3(neighborPixel, 0)));
                const SurfaceData neighborSurface = LoadGBufferSurface(uint2(neighborPixel));
                const float targetPdfAtNeighbor = ReSTIRDIGetTargetPdf(reservoir, neighborSurface);
                if (selectedNeighborIndex == int(neighborIndex))
                {
                    normalizationNumerator = targetPdfAtNeighbor;
                }
                normalizationDenominator += targetPdfAtNeighbor * neighbor.M;
            }
            ReSTIRDIFinalizeResampling(reservoir, normalizationNumerator, normalizationDenominator);
        }
    }
    ReSTIRDISpatialReservoir[pixel] = ReSTIRDIPackReservoir(reservoir);
}
//Modify End
