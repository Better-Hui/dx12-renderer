//Modify Begin:2026-08-05 by BestHui
#define RAYTRACING_DEMO_RESTIR_DI 1

#include "../../../Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rayquery.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"

Texture2D<uint4> ReSTIRDIRISReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float2> MotionVectorTexture : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDIHistoryReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDICurrentReservoir : register(u3);
RWTexture2D<float4> ReSTIRDIHistoryPosition : register(u4);
RWTexture2D<float4> ReSTIRDICurrentPosition : register(u5);
RWTexture2D<float4> ReSTIRDIHistoryNormalRoughness : register(u6);
RWTexture2D<float4> ReSTIRDICurrentNormalRoughness : register(u7);
RWTexture2D<float4> ReSTIRDIHistoryDiffuseMetallic : register(u8);
RWTexture2D<float4> ReSTIRDICurrentDiffuseMetallic : register(u9);
RWTexture2D<float4> ReSTIRDIHistorySpecularOcclusion : register(u10);
RWTexture2D<float4> ReSTIRDICurrentSpecularOcclusion : register(u11);

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

SurfaceData LoadReSTIRDIHistorySurface(const uint2 pixel)
{
    SurfaceData surface;
    surface.Diffuse = 0.0f;
    surface.Specular = 0.0f;
    surface.PositionWs = 0.0f;
    surface.NormalWs = float3(0.0f, 1.0f, 0.0f);
    surface.PositionError = 0.0f;
    surface.Metallic = 0.0f;
    surface.Roughness = 1.0f;
    surface.AmbientOcclusion = 1.0f;
    surface.Valid = false;

    const float4 positionAndDepth = ReSTIRDIHistoryPosition[pixel];
    if (positionAndDepth.w <= 0.0f)
    {
        return surface;
    }

    const float4 normalRoughness = ReSTIRDIHistoryNormalRoughness[pixel];
    const float4 diffuseMetallic = ReSTIRDIHistoryDiffuseMetallic[pixel];
    const float4 specularOcclusion = ReSTIRDIHistorySpecularOcclusion[pixel];
    surface.PositionWs = positionAndDepth.xyz;
    surface.PositionError = ComputePositionError(surface.PositionWs);
    surface.NormalWs = normalize(normalRoughness.xyz);
    surface.Roughness = saturate(normalRoughness.w);
    surface.Diffuse = saturate(diffuseMetallic.rgb);
    surface.Metallic = saturate(diffuseMetallic.w);
    surface.Specular = saturate(specularOcclusion.rgb);
    surface.AmbientOcclusion = saturate(specularOcclusion.w);
    surface.Valid = true;
    return surface;
}

void StoreReSTIRDIHistorySurface(const uint2 pixel, const SurfaceData surface, const float depth)
{
    ReSTIRDICurrentPosition[pixel] = surface.Valid ? float4(surface.PositionWs, depth) : 0.0f;
    ReSTIRDICurrentNormalRoughness[pixel] = surface.Valid ? float4(normalize(surface.NormalWs), surface.Roughness) : 0.0f;
    ReSTIRDICurrentDiffuseMetallic[pixel] = surface.Valid ? float4(surface.Diffuse, surface.Metallic) : 0.0f;
    ReSTIRDICurrentSpecularOcclusion[pixel] = surface.Valid ? float4(surface.Specular, surface.AmbientOcclusion) : 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    const ReSTIRDIReservoir currentReservoir = ReSTIRDIUnpackReservoir(ReSTIRDIRISReservoir.Load(int3(pixel, 0)));
    ReSTIRDIReservoir reservoir = ReSTIRDIEmptyReservoir();
    const SurfaceData surface = LoadGBufferSurface(pixel);
    const uint totalLightCount = GetReSTIRDILightCount();
    const float receiverDepth = surface.Valid ? length(surface.PositionWs - Camera_Position.xyz) : 0.0f;
    bool selectedHistorySample = false;
    float historyM = 0.0f;
    SurfaceData historySurface;
    historySurface.Valid = false;
    if (surface.Valid && ReSTIRDIIsValid(currentReservoir))
    {
        ReSTIRDICombineReservoirs(
            reservoir,
            currentReservoir,
            currentReservoir.SelectedTargetPdf,
            0.5f);
    }
    if (surface.Valid && ReSTIRDI_TemporalResamplingEnabled != 0u && ReSTIRDI_HistoryValid != 0u && totalLightCount != 0u)
    {
        uint rngState = InitializeRandomState(pixel, Camera_Width, Camera_FrameIndex, 0x0f16c4a3u);
        const float2 motionVector = MotionVectorTexture.Load(int3(pixel, 0));
        const float2 reprojectedPosition =
            float2(pixel) +
            motionVector * float2(Camera_Width, Camera_Height) +
            float2(Random01(rngState), Random01(rngState)) -
            0.5f;
        const int2 reprojectedPixel = int2(round(reprojectedPosition));
        [unroll]
        for (uint searchIndex = 0u; searchIndex < 9u; ++searchIndex)
        {
            const int2 searchOffset = searchIndex == 0u
                ? int2(0, 0)
                : int2(
                    int((Random01(rngState) - 0.5f) * 4.0f),
                    int((Random01(rngState) - 0.5f) * 4.0f));
            const int2 historyPixel = reprojectedPixel + searchOffset;
            if (!all(historyPixel >= 0) || historyPixel.x >= int(Camera_Width) || historyPixel.y >= int(Camera_Height))
            {
                continue;
            }

            ReSTIRDIReservoir history = ReSTIRDIUnpackReservoir(ReSTIRDIHistoryReservoir[historyPixel]);
            historySurface = LoadReSTIRDIHistorySurface(historyPixel);
            const bool surfacesCompatible = ReSTIRDIHaveCompatibleSurfaces(
                surface,
                receiverDepth,
                historySurface,
                ReSTIRDIHistoryPosition[historyPixel].w,
                ReSTIRDI_TemporalNormalSimilarityThreshold,
                ReSTIRDI_DepthSimilarityThreshold,
                ReSTIRDI_MaterialSimilarityThreshold);
            if (!surfacesCompatible)
            {
                continue;
            }

            if (ReSTIRDIIsValid(history) && history.LightIndex < totalLightCount)
            {
                history.M = min(
                    history.M,
                    float(ReSTIRDI_TemporalMaxHistoryLength) * max(1.0f, currentReservoir.M));
                const ReSTIRDIDirectLightSample historySample = SampleReSTIRDIDirectLight(history.LightIndex, surface, history.SampleSeed);
                const bool visible = !ReSTIRDIShouldTestVisibility(
                    ReSTIRDI_VisibilityTestMask,
                    ReSTIRDIVisibilityStageTemporal) ||
                    IsReSTIRDIDirectLightSampleVisible(surface, historySample);
                if (visible)
                {
                    historyM = history.M;
                    const float targetPdf = max(0.0f, Luminance(historySample.UnshadowedContribution));
                    selectedHistorySample = ReSTIRDICombineReservoirs(
                        reservoir,
                        history,
                        targetPdf,
                        Random01(rngState));
                }
            }
            break;
        }
    }

    if (ReSTIRDIIsValid(reservoir))
    {
        const ReSTIRDIDirectLightSample selectedAtCurrent = SampleReSTIRDIDirectLight(
            reservoir.LightIndex,
            surface,
            reservoir.SampleSeed);
        float normalizationNumerator = reservoir.SelectedTargetPdf;
        float normalizationDenominator = reservoir.SelectedTargetPdf * currentReservoir.M;
        if (historyM > 0.0f && historySurface.Valid)
        {
            const ReSTIRDIDirectLightSample selectedAtHistory = SampleReSTIRDIDirectLight(
                reservoir.LightIndex,
                historySurface,
                reservoir.SampleSeed);
            const float historyTargetPdf = max(0.0f, Luminance(selectedAtHistory.UnshadowedContribution));
            if (selectedHistorySample)
            {
                normalizationNumerator = historyTargetPdf;
            }
            normalizationDenominator += historyTargetPdf * historyM;
        }
        ReSTIRDIFinalizeResampling(reservoir, normalizationNumerator, normalizationDenominator);
    }
    ReSTIRDICurrentReservoir[pixel] = ReSTIRDIPackReservoir(reservoir);
    StoreReSTIRDIHistorySurface(pixel, surface, receiverDepth);
}
//Modify End
