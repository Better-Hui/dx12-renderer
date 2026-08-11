//Modify Begin:2026-07-30 by BestHui
#include "ReSTIRDISceneContract.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

Texture2D<uint4> ReSTIRDIRISReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRDIRISReservoirState : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float2> MotionVectorTexture : register(t14, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRDIHistoryReservoir : register(t15, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRDIHistoryReservoirState : register(t16, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> ReSTIRDIHistoryPosition : register(t17, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> ReSTIRDIHistoryNormalRoughness : register(t18, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> ReSTIRDIHistoryDiffuseMetallic : register(t19, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> ReSTIRDIHistorySpecularOcclusion : register(t20, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDITemporalReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDITemporalReservoirState : register(u3);

#include "ReSTIRDI/ReSTIRDISurface.hlsli"

#if RESTIR_DI_USE_TEMPORAL_BOILING_FILTER
static const uint ReSTIRDIBoilingFilterGroupSize = 8u;
static const uint ReSTIRDIBoilingFilterSharedWaveCount = 2u;
groupshared float ReSTIRDIBoilingFilterWeights[ReSTIRDIBoilingFilterSharedWaveCount];
groupshared uint ReSTIRDIBoilingFilterCounts[ReSTIRDIBoilingFilterSharedWaveCount];
#endif

ReSTIRDI_Surface ReSTIRDI_LoadHistorySurface(const uint2 pixel)
{
    ReSTIRDI_Surface surface = (ReSTIRDI_Surface)0;
    const float4 positionAndDepth = ReSTIRDIHistoryPosition.Load(int3(pixel, 0));
    if (positionAndDepth.w <= 0.0f)
    {
        return surface;
    }

    const float4 normalRoughness = ReSTIRDIHistoryNormalRoughness.Load(int3(pixel, 0));
    const float4 diffuseMetallic = ReSTIRDIHistoryDiffuseMetallic.Load(int3(pixel, 0));
    const float4 specularOcclusion = ReSTIRDIHistorySpecularOcclusion.Load(int3(pixel, 0));
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

#if RESTIR_DI_USE_TEMPORAL_BOILING_FILTER
void ApplyReSTIRDIBoilingFilter(const uint2 localIndex, inout ReSTIRDIReservoir reservoir)
{
    const uint linearIndex = localIndex.x + localIndex.y * ReSTIRDIBoilingFilterGroupSize;
    float waveWeight = WaveActiveSum(reservoir.WeightSum);
    uint waveCount = WaveActiveCountBits(reservoir.WeightSum > 0.0f);
    const uint waveIndex = linearIndex / WaveGetLaneCount();
    if (WaveIsFirstLane())
    {
        ReSTIRDIBoilingFilterWeights[waveIndex] = waveWeight;
        ReSTIRDIBoilingFilterCounts[waveIndex] = waveCount;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint reductionLaneCount = (ReSTIRDIBoilingFilterGroupSize * ReSTIRDIBoilingFilterGroupSize + WaveGetLaneCount() - 1u) / WaveGetLaneCount();
    if (linearIndex < reductionLaneCount)
    {
        waveWeight = ReSTIRDIBoilingFilterWeights[linearIndex];
        waveCount = ReSTIRDIBoilingFilterCounts[linearIndex];
        waveWeight = WaveActiveSum(waveWeight);
        waveCount = WaveActiveSum(waveCount);
        if (linearIndex == 0u)
        {
            ReSTIRDIBoilingFilterWeights[0] = waveCount > 0u ? waveWeight / float(waveCount) : 0.0f;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const float thresholdMultiplier = 10.0f / clamp(ReSTIRDI_BoilingFilterStrength, 0.000001f, 1.0f) - 9.0f;
    if (reservoir.WeightSum > ReSTIRDIBoilingFilterWeights[0] * thresholdMultiplier)
    {
        reservoir = ReSTIRDIEmptyReservoir();
    }
}
#endif

#if RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING
int2 ApplyReSTIRDIPermutationSampling(const int2 pixel)
{
    const uint uniformRandomNumber = ReSTIRDI_FrameIndex * 747796405u + 2891336453u;
    const int2 offset = int2(uniformRandomNumber & 3u, (uniformRandomNumber >> 2u) & 3u);
    int2 permutedPixel = pixel + offset;
    permutedPixel.x ^= 3;
    permutedPixel.y ^= 3;
    return permutedPixel - offset;
}
#endif

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const bool pixelInBounds = pixel.x < ReSTIRDI_ScreenWidth && pixel.y < ReSTIRDI_ScreenHeight;
    ReSTIRDIReservoir result = ReSTIRDIEmptyReservoir();

    if (pixelInBounds)
    {
        const ReSTIRDI_Surface surface = ReSTIRDI_LoadSurface(pixel);
        const ReSTIRDIReservoir current = ReSTIRDIUnpackReservoir(
            ReSTIRDIRISReservoir.Load(int3(pixel, 0)), ReSTIRDIRISReservoirState.Load(int3(pixel, 0)));
        if (surface.Valid)
        {
            ReSTIRDICombineReservoirs(result, current, 0.5f, current.SelectedTargetPdf);
#if RESTIR_DI_USE_TEMPORAL_REUSE
            if (ReSTIRDI_HistoryValid != 0u)
            {
                uint rngState = ReSTIRDI_InitializeRandomState(pixel, ReSTIRDI_ScreenWidth, ReSTIRDI_FrameIndex, 0x0f16c4a3u);
                float2 motion = MotionVectorTexture.Load(int3(pixel, 0)) * float2(ReSTIRDI_ScreenWidth, ReSTIRDI_ScreenHeight);
#if !RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING
                    motion += float2(ReSTIRDI_Random01(rngState), ReSTIRDI_Random01(rngState)) - 0.5f;
#endif

                const int2 reprojectedPixel = int2(round(float2(pixel) + motion));
                const float receiverDepth = length(surface.PositionWs - ReSTIRDI_CameraPosition.xyz);
                ReSTIRDIReservoir history = ReSTIRDIEmptyReservoir();
                ReSTIRDI_Surface historySurface = (ReSTIRDI_Surface)0;
                int2 selectedPixel = int2(-1, -1);
                [unroll]
                for (uint searchIndex = 0u; searchIndex < 9u; ++searchIndex)
                {
                    const int2 offset = searchIndex == 0u
                        ? int2(0, 0)
                        : int2(int((ReSTIRDI_Random01(rngState) - 0.5f) * 4.0f), int((ReSTIRDI_Random01(rngState) - 0.5f) * 4.0f));
                    int2 candidatePixel = reprojectedPixel + offset;
#if RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING
                    if (searchIndex == 0u)
                    {
                        candidatePixel = ApplyReSTIRDIPermutationSampling(candidatePixel);
                    }
#endif
                    if (any(candidatePixel < 0) || candidatePixel.x >= int(ReSTIRDI_ScreenWidth) || candidatePixel.y >= int(ReSTIRDI_ScreenHeight))
                    {
                        continue;
                    }

                    const ReSTIRDI_Surface candidateSurface = ReSTIRDI_LoadHistorySurface(uint2(candidatePixel));
                    const float candidateDepth = ReSTIRDIHistoryPosition.Load(int3(candidatePixel, 0)).w;
                    if (!candidateSurface.Valid || !ReSTIRDIIsSurfaceCompatible(
                        surface.NormalWs,
                        receiverDepth,
                        candidateSurface.NormalWs,
                        candidateDepth,
                        ReSTIRDI_TemporalNormalSimilarityThreshold,
                        ReSTIRDI_TemporalDepthSimilarityThreshold))
                    {
                        continue;
                    }

                    history = ReSTIRDIUnpackReservoir(
                        ReSTIRDIHistoryReservoir.Load(int3(candidatePixel, 0)),
                        ReSTIRDIHistoryReservoirState.Load(int3(candidatePixel, 0)));
                    historySurface = candidateSurface;
                    selectedPixel = candidatePixel;
                    break;
                }

                const uint currentLightIndex = ReSTIRDIIsValid(current) ? ReSTIRDIGetLightIndex(current) : ReSTIRDILightIndexMask;
                uint selectedLightPreviousIndex = currentLightIndex;
                bool selectedPreviousSample = false;
                float previousM = 0.0f;
                if (selectedPixel.x >= 0)
                {
                    history.M = min(history.M, float(ReSTIRDI_TemporalMaxHistoryLength) * max(1.0f, current.M));
                    history.SpatialDistance += selectedPixel - reprojectedPixel;
                    history.Age = min(history.Age + 1u, 255u);
                    previousM = history.M;

                    float historyTargetPdf = 0.0f;
                    if (ReSTIRDIIsValid(history) && ReSTIRDIGetLightIndex(history) < ReSTIRDI_GetLightCount())
                    {
                        const ReSTIRDI_LightSample sample = ReSTIRDI_SampleLight(
                            ReSTIRDIGetLightIndex(history), surface, ReSTIRDIGetSampleUv(history));
                        historyTargetPdf = sample.Valid ? max(0.0f, ReSTIRDI_Luminance(sample.UnshadowedContribution)) : 0.0f;
                    }

                    selectedPreviousSample = ReSTIRDICombineReservoirs(
                        result, history, ReSTIRDI_Random01(rngState), historyTargetPdf);
                    if (selectedPreviousSample && ReSTIRDIIsValid(history))
                    {
                        selectedLightPreviousIndex = ReSTIRDIGetLightIndex(history);
                    }
                }

#if RESTIR_DI_TEMPORAL_BIAS_MODE != 0
                if (ReSTIRDIIsValid(result))
                {
                    float normalizationNumerator = result.SelectedTargetPdf;
                    float normalizationDenominator = result.SelectedTargetPdf * current.M;
                    if (selectedPixel.x >= 0 && previousM > 0.0f && selectedLightPreviousIndex < ReSTIRDI_GetLightCount())
                    {
                        const ReSTIRDI_LightSample selectedSampleAtHistory = ReSTIRDI_SampleLight(
                            selectedLightPreviousIndex,
                            historySurface,
                            ReSTIRDIGetSampleUv(result));
                        float temporalTargetPdf = selectedSampleAtHistory.Valid
                            ? max(0.0f, ReSTIRDI_Luminance(selectedSampleAtHistory.UnshadowedContribution))
                            : 0.0f;
#if RESTIR_DI_TEMPORAL_BIAS_MODE == 2
#if RESTIR_DI_USE_TEMPORAL_VISIBILITY_SHORTCUT
                        if (temporalTargetPdf > 0.0f && !selectedPreviousSample &&
                            !ReSTIRDI_TestVisibility(historySurface, selectedSampleAtHistory))
#else
                        if (temporalTargetPdf > 0.0f &&
                            !ReSTIRDI_TestVisibility(historySurface, selectedSampleAtHistory))
#endif
                        {
                            temporalTargetPdf = 0.0f;
                        }
#endif
                        normalizationNumerator = selectedPreviousSample ? temporalTargetPdf : normalizationNumerator;
                        normalizationDenominator += temporalTargetPdf * previousM;
                    }
                    ReSTIRDIFinalizeResampling(result, normalizationNumerator, normalizationDenominator);
                }
                else
                {
                    ReSTIRDIFinalizeResampling(result, 1.0f, result.M);
                }
#else
                ReSTIRDIFinalizeResampling(result, 1.0f, result.M);
#endif
            }
            else
            {
                ReSTIRDIFinalizeResampling(result, 1.0f, result.M);
            }
#else
            ReSTIRDIFinalizeResampling(result, 1.0f, result.M);
#endif
        }
    }

#if RESTIR_DI_USE_TEMPORAL_BOILING_FILTER
    ApplyReSTIRDIBoilingFilter(groupThreadId.xy, result);
#endif
    if (pixelInBounds)
    {
        ReSTIRDITemporalReservoir[pixel] = ReSTIRDIPackReservoirCore(result);
        ReSTIRDITemporalReservoirState[pixel] = ReSTIRDIPackReservoirState(result);
    }
}
//Modify End
