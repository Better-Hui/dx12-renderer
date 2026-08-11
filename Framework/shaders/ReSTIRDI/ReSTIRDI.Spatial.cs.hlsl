//Modify Begin:2026-07-30 by BestHui
#include "ReSTIRDISceneContract.hlsli"
#include "ReSTIRDI/ReSTIRDI.hlsli"
#include "ReSTIRDI/ReSTIRDIConstants.hlsli"

Texture2D<uint4> ReSTIRDITemporalReservoir : register(t12, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<uint4> ReSTIRDITemporalReservoirState : register(t13, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWTexture2D<uint4> ReSTIRDISpatialReservoir : register(u2);
RWTexture2D<uint4> ReSTIRDISpatialReservoirState : register(u3);

#include "ReSTIRDI/ReSTIRDISurface.hlsli"

static const float2 ReSTIRDISpatialOffsets[32] =
{
    float2(0.000f, -0.450f), float2(0.550f, 0.150f),
    float2(-0.500f, 0.200f), float2(0.200f, 0.650f),
    float2(-0.150f, -0.800f), float2(0.800f, -0.350f),
    float2(-0.750f, -0.450f), float2(0.550f, 0.650f),
    float2(-0.600f, 0.700f), float2(0.900f, 0.200f),
    float2(-0.900f, 0.100f), float2(0.250f, -0.950f),
    float2(-0.250f, 0.350f), float2(0.100f, 0.900f),
    float2(-0.450f, -0.100f), float2(0.450f, -0.600f),
    float2(0.050f, -0.150f), float2(-0.300f, -0.550f),
    float2(0.680f, -0.720f), float2(-0.920f, 0.520f),
    float2(0.730f, 0.940f), float2(-0.050f, 0.520f),
    float2(0.360f, -0.920f), float2(-0.700f, -0.170f),
    float2(0.940f, -0.020f), float2(-0.620f, 0.940f),
    float2(0.150f, 0.120f), float2(-0.100f, -0.320f),
    float2(0.310f, 0.430f), float2(-0.430f, 0.040f),
    float2(0.970f, 0.580f), float2(-0.820f, -0.880f)
};

float ReSTIRDIGetTargetPdf(const ReSTIRDIReservoir reservoir, const ReSTIRDI_Surface surface)
{
    if (!ReSTIRDIIsValid(reservoir) || ReSTIRDIGetLightIndex(reservoir) >= ReSTIRDI_GetLightCount())
    {
        return 0.0f;
    }

    const ReSTIRDI_LightSample sample = ReSTIRDI_SampleLight(
        ReSTIRDIGetLightIndex(reservoir), surface, ReSTIRDIGetSampleUv(reservoir));
    return sample.Valid ? max(0.0f, ReSTIRDI_Luminance(sample.UnshadowedContribution)) : 0.0f;
}

bool ReSTIRDIHaveCompatibleSpatialSurfaces(
    const ReSTIRDI_Surface centerSurface,
    const float centerDepth,
    const ReSTIRDI_Surface neighborSurface,
    const float neighborDepth)
{
    if (!neighborSurface.Valid || !ReSTIRDIIsSurfaceCompatible(
        centerSurface.NormalWs,
        centerDepth,
        neighborSurface.NormalWs,
        neighborDepth,
        ReSTIRDI_SpatialNormalSimilarityThreshold,
        ReSTIRDI_SpatialDepthSimilarityThreshold))
    {
        return false;
    }

#if RESTIR_DI_USE_SPATIAL_MATERIAL_SIMILARITY
    return ReSTIRDIHaveSimilarMaterials(
        centerSurface,
        neighborSurface,
        ReSTIRDI_SpatialMaterialSimilarityThreshold);
#else
    return true;
#endif
}

int2 ReSTIRDIGetSpatialNeighborPixel(const uint2 pixel, const uint sampleIndex)
{
    const int2 offset = int2(ReSTIRDISpatialOffsets[sampleIndex & 31u] * ReSTIRDI_SpatialSamplingRadius);
    return clamp(
        int2(pixel) + offset,
        int2(0, 0),
        int2(int(ReSTIRDI_ScreenWidth) - 1, int(ReSTIRDI_ScreenHeight) - 1));
}

bool ReSTIRDIStreamNeighborWithPairwiseMis(
    inout ReSTIRDIReservoir state,
    const float randomSample,
    const ReSTIRDIReservoir neighborReservoir,
    const ReSTIRDI_Surface neighborSurface,
    const ReSTIRDIReservoir canonicalReservoir,
    const ReSTIRDI_Surface canonicalSurface,
    const uint neighborCount)
{
    const float neighborWeightAtCanonical = ReSTIRDIGetTargetPdf(neighborReservoir, canonicalSurface);
    const float canonicalWeightAtNeighbor = ReSTIRDIGetTargetPdf(canonicalReservoir, neighborSurface);
    const float neighborWeightAtNeighbor = ReSTIRDIGetTargetPdf(neighborReservoir, neighborSurface);
    const float canonicalWeightAtCanonical = ReSTIRDIGetTargetPdf(canonicalReservoir, canonicalSurface);
    const float firstMisWeight = ReSTIRDIPairwiseMisWeight(
        neighborWeightAtNeighbor,
        neighborWeightAtCanonical,
        neighborReservoir.M * float(neighborCount),
        canonicalReservoir.M);
    const float secondMisWeight = ReSTIRDIPairwiseMisWeight(
        canonicalWeightAtNeighbor,
        canonicalWeightAtCanonical,
        neighborReservoir.M * float(neighborCount),
        canonicalReservoir.M);
    const float effectiveM = neighborReservoir.M * min(
        ReSTIRDIPairwiseMFactor(neighborWeightAtNeighbor, neighborWeightAtCanonical),
        ReSTIRDIPairwiseMFactor(canonicalWeightAtNeighbor, canonicalWeightAtCanonical));

    state.CanonicalWeight += 1.0f - secondMisWeight;
    return ReSTIRDIInternalSimpleResample(
        state,
        neighborReservoir,
        randomSample,
        neighborWeightAtCanonical,
        neighborReservoir.WeightSum * firstMisWeight,
        effectiveM);
}

ReSTIRDIReservoir ReSTIRDIPairwiseSpatialResampling(
    const uint2 pixel,
    const ReSTIRDI_Surface centerSurface,
    const ReSTIRDIReservoir centerReservoir,
    inout uint rngState,
    const uint neighborCount)
{
    ReSTIRDIReservoir result = ReSTIRDIEmptyReservoir();
    uint validSpatialSampleCount = 0u;
    const uint offsetStart = uint(ReSTIRDI_Random01(rngState) * 32.0f) & 31u;
    const float centerDepth = length(centerSurface.PositionWs - ReSTIRDI_CameraPosition.xyz);
    [loop]
    for (uint neighborIndex = 0u; neighborIndex < neighborCount; ++neighborIndex)
    {
        const uint offsetIndex = (offsetStart + neighborIndex) & 31u;
        const int2 neighborPixel = ReSTIRDIGetSpatialNeighborPixel(pixel, offsetIndex);
        const ReSTIRDI_Surface neighborSurface = ReSTIRDI_LoadSurface(uint2(neighborPixel));
        const float neighborDepth = length(neighborSurface.PositionWs - ReSTIRDI_CameraPosition.xyz);
        if (!ReSTIRDIHaveCompatibleSpatialSurfaces(centerSurface, centerDepth, neighborSurface, neighborDepth))
        {
            continue;
        }

        ReSTIRDIReservoir neighborReservoir = ReSTIRDIUnpackReservoir(
            ReSTIRDITemporalReservoir.Load(int3(neighborPixel, 0)),
            ReSTIRDITemporalReservoirState.Load(int3(neighborPixel, 0)));
#if RESTIR_DI_USE_SPATIAL_NAIVE_SAMPLE_DISCOUNT
        if (ReSTIRDIIsValid(neighborReservoir) &&
            neighborReservoir.M <= ReSTIRDINaiveSamplingMThreshold)
        {
            continue;
        }
#endif

        const int2 spatialOffset = int2(ReSTIRDISpatialOffsets[offsetIndex] * ReSTIRDI_SpatialSamplingRadius);
        neighborReservoir.SpatialDistance += spatialOffset;
        ++validSpatialSampleCount;
        if (neighborReservoir.M > 0.0f)
        {
            ReSTIRDIStreamNeighborWithPairwiseMis(
                result,
                ReSTIRDI_Random01(rngState),
                neighborReservoir,
                neighborSurface,
                centerReservoir,
                centerSurface,
                neighborCount);
        }
    }

    result.CanonicalWeight = validSpatialSampleCount == 0u ? 1.0f : result.CanonicalWeight;
    ReSTIRDIInternalSimpleResample(
        result,
        centerReservoir,
        ReSTIRDI_Random01(rngState),
        centerReservoir.SelectedTargetPdf,
        centerReservoir.WeightSum * result.CanonicalWeight,
        centerReservoir.M);
    ReSTIRDIFinalizeResampling(result, 1.0f, float(max(1u, validSpatialSampleCount)));
    return result;
}

ReSTIRDIReservoir ReSTIRDIStandardSpatialResampling(
    const uint2 pixel,
    const ReSTIRDI_Surface centerSurface,
    const ReSTIRDIReservoir centerReservoir,
    inout uint rngState,
    const uint neighborCount)
{
    ReSTIRDIReservoir result = ReSTIRDIEmptyReservoir();
    ReSTIRDICombineReservoirs(result, centerReservoir, 0.5f, centerReservoir.SelectedTargetPdf);
    uint selectedNeighbor = 0xffffffffu;
    uint selectedLightIndex = ReSTIRDIIsValid(centerReservoir)
        ? ReSTIRDIGetLightIndex(centerReservoir)
        : ReSTIRDILightIndexMask;
    const uint offsetStart = uint(ReSTIRDI_Random01(rngState) * 32.0f) & 31u;
    uint validNeighborMask = 0u;
    const float centerDepth = length(centerSurface.PositionWs - ReSTIRDI_CameraPosition.xyz);

    [loop]
    for (uint neighborIndex = 0u; neighborIndex < neighborCount; ++neighborIndex)
    {
        const uint offsetIndex = (offsetStart + neighborIndex) & 31u;
        const int2 neighborPixel = ReSTIRDIGetSpatialNeighborPixel(pixel, offsetIndex);
        const ReSTIRDI_Surface neighborSurface = ReSTIRDI_LoadSurface(uint2(neighborPixel));
        const float neighborDepth = length(neighborSurface.PositionWs - ReSTIRDI_CameraPosition.xyz);
        if (!ReSTIRDIHaveCompatibleSpatialSurfaces(centerSurface, centerDepth, neighborSurface, neighborDepth))
        {
            continue;
        }

        validNeighborMask |= 1u << neighborIndex;
        ReSTIRDIReservoir neighborReservoir = ReSTIRDIUnpackReservoir(
            ReSTIRDITemporalReservoir.Load(int3(neighborPixel, 0)),
            ReSTIRDITemporalReservoirState.Load(int3(neighborPixel, 0)));
        const int2 spatialOffset = int2(ReSTIRDISpatialOffsets[offsetIndex] * ReSTIRDI_SpatialSamplingRadius);
        neighborReservoir.SpatialDistance += spatialOffset;
#if RESTIR_DI_USE_SPATIAL_NAIVE_SAMPLE_DISCOUNT
        if (ReSTIRDIIsValid(neighborReservoir) &&
            neighborReservoir.M <= ReSTIRDINaiveSamplingMThreshold)
        {
            continue;
        }
#endif

        const float neighborTargetPdf = ReSTIRDIGetTargetPdf(neighborReservoir, centerSurface);
        if (ReSTIRDICombineReservoirs(result, neighborReservoir, ReSTIRDI_Random01(rngState), neighborTargetPdf))
        {
            selectedNeighbor = neighborIndex;
            selectedLightIndex = ReSTIRDIIsValid(neighborReservoir)
                ? ReSTIRDIGetLightIndex(neighborReservoir)
                : ReSTIRDILightIndexMask;
        }
    }

    if (!ReSTIRDIIsValid(result))
    {
        return result;
    }

#if RESTIR_DI_SPATIAL_BIAS_MODE == 0
    ReSTIRDIFinalizeResampling(result, 1.0f, result.M);
    return result;
#endif

    float normalizationNumerator = result.SelectedTargetPdf;
    float normalizationDenominator = result.SelectedTargetPdf * centerReservoir.M;
    [loop]
    for (uint normalizationNeighborIndex = 0u; normalizationNeighborIndex < neighborCount; ++normalizationNeighborIndex)
    {
        if ((validNeighborMask & (1u << normalizationNeighborIndex)) == 0u)
        {
            continue;
        }

        const uint offsetIndex = (offsetStart + normalizationNeighborIndex) & 31u;
        const int2 neighborPixel = ReSTIRDIGetSpatialNeighborPixel(pixel, offsetIndex);
        const ReSTIRDI_Surface neighborSurface = ReSTIRDI_LoadSurface(uint2(neighborPixel));
        ReSTIRDIReservoir neighborReservoir = ReSTIRDIUnpackReservoir(
            ReSTIRDITemporalReservoir.Load(int3(neighborPixel, 0)),
            ReSTIRDITemporalReservoirState.Load(int3(neighborPixel, 0)));
        float targetPdfAtNeighbor = 0.0f;
        if (selectedLightIndex < ReSTIRDI_GetLightCount())
        {
            const ReSTIRDI_LightSample selectedSampleAtNeighbor = ReSTIRDI_SampleLight(
                selectedLightIndex,
                neighborSurface,
                ReSTIRDIGetSampleUv(result));
            targetPdfAtNeighbor = selectedSampleAtNeighbor.Valid
                ? max(0.0f, ReSTIRDI_Luminance(selectedSampleAtNeighbor.UnshadowedContribution))
                : 0.0f;
#if RESTIR_DI_SPATIAL_BIAS_MODE == 3
            if (targetPdfAtNeighbor > 0.0f &&
                !ReSTIRDI_TestVisibility(neighborSurface, selectedSampleAtNeighbor))
            {
                targetPdfAtNeighbor = 0.0f;
            }
#endif
        }

        if (selectedNeighbor == normalizationNeighborIndex)
        {
            normalizationNumerator = targetPdfAtNeighbor;
        }
        normalizationDenominator += targetPdfAtNeighbor * neighborReservoir.M;
    }

    ReSTIRDIFinalizeResampling(result, normalizationNumerator, normalizationDenominator);
    return result;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= ReSTIRDI_ScreenWidth || pixel.y >= ReSTIRDI_ScreenHeight)
    {
        return;
    }

    const ReSTIRDIReservoir temporalReservoir = ReSTIRDIUnpackReservoir(
        ReSTIRDITemporalReservoir.Load(int3(pixel, 0)), ReSTIRDITemporalReservoirState.Load(int3(pixel, 0)));
    ReSTIRDIReservoir result = temporalReservoir;
    const ReSTIRDI_Surface surface = ReSTIRDI_LoadSurface(pixel);
#if RESTIR_DI_USE_SPATIAL_REUSE
    if (surface.Valid && ReSTIRDI_SpatialNeighborCount > 0u)
    {
        uint rngState = ReSTIRDI_InitializeRandomState(pixel, ReSTIRDI_ScreenWidth, ReSTIRDI_FrameIndex, 0x7a3d1f0bu);
        uint neighborCount = ReSTIRDI_SpatialNeighborCount;
        if (temporalReservoir.M < float(ReSTIRDI_SpatialTargetHistoryLength))
        {
            neighborCount = max(neighborCount, ReSTIRDI_SpatialDisocclusionBoostSampleCount);
        }
        neighborCount = min(neighborCount, 32u);
#if RESTIR_DI_SPATIAL_BIAS_MODE == 2
        if (ReSTIRDIIsValid(temporalReservoir))
        {
            result = ReSTIRDIPairwiseSpatialResampling(pixel, surface, temporalReservoir, rngState, neighborCount);
        }
        else
        {
            result = ReSTIRDIStandardSpatialResampling(pixel, surface, temporalReservoir, rngState, neighborCount);
        }
#else
        result = ReSTIRDIStandardSpatialResampling(pixel, surface, temporalReservoir, rngState, neighborCount);
#endif
    }
#endif

    ReSTIRDISpatialReservoir[pixel] = ReSTIRDIPackReservoirCore(result);
    ReSTIRDISpatialReservoirState[pixel] = ReSTIRDIPackReservoirState(result);
}
//Modify End
