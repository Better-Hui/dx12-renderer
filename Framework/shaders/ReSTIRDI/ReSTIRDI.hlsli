#ifndef FRAMEWORK_RESTIR_DI_HLSLI
#define FRAMEWORK_RESTIR_DI_HLSLI

//Modify Begin:2026-07-30 by BestHui
struct ReSTIRDIReservoir
{
    uint LightData;
    uint UvData;
    float WeightSum;
    float SelectedTargetPdf;
    float M;
    uint PackedVisibility;
    int2 SpatialDistance;
    uint Age;
    float CanonicalWeight;
};

static const uint ReSTIRDILightValidBit = 0x80000000u;
static const uint ReSTIRDILightIndexMask = 0x7fffffffu;
static const uint ReSTIRDIVisibilityMask = 0x0003ffffu;
static const uint ReSTIRDIVisibilityChannelMask = 0x3fu;
static const uint ReSTIRDIVisibilityChannelShift = 6u;
static const uint ReSTIRDIMShift = 18u;
static const uint ReSTIRDIMaxM = 0x3fffu;
static const float ReSTIRDINaiveSamplingMThreshold = 1.0f;

ReSTIRDIReservoir ReSTIRDIEmptyReservoir()
{
    ReSTIRDIReservoir reservoir;
    reservoir.LightData = 0u;
    reservoir.UvData = 0u;
    reservoir.WeightSum = 0.0f;
    reservoir.SelectedTargetPdf = 0.0f;
    reservoir.M = 0.0f;
    reservoir.PackedVisibility = 0u;
    reservoir.SpatialDistance = int2(0, 0);
    reservoir.Age = 0u;
    reservoir.CanonicalWeight = 0.0f;
    return reservoir;
}

bool ReSTIRDIIsValid(const ReSTIRDIReservoir reservoir)
{
    return reservoir.LightData != 0u;
}

uint ReSTIRDIGetLightIndex(const ReSTIRDIReservoir reservoir)
{
    return reservoir.LightData & ReSTIRDILightIndexMask;
}

float2 ReSTIRDIGetSampleUv(const ReSTIRDIReservoir reservoir)
{
    return float2(reservoir.UvData & 0xffffu, reservoir.UvData >> 16u) / 65535.0f;
}

uint ReSTIRDIEncodeSampleUv(const float2 uv)
{
    const uint2 encoded = uint2(saturate(uv) * 65535.0f);
    return encoded.x | (encoded.y << 16u);
}

void ReSTIRDIStoreVisibility(inout ReSTIRDIReservoir reservoir, const float visibility, const bool discardIfInvisible)
{
    const uint encoded = uint(saturate(visibility) * float(ReSTIRDIVisibilityChannelMask));
    reservoir.PackedVisibility = encoded |
        (encoded << ReSTIRDIVisibilityChannelShift) |
        (encoded << (ReSTIRDIVisibilityChannelShift * 2u));
    reservoir.SpatialDistance = int2(0, 0);
    reservoir.Age = 0u;
    if (discardIfInvisible && encoded == 0u)
    {
        reservoir.LightData = 0u;
        reservoir.WeightSum = 0.0f;
    }
}

bool ReSTIRDIGetReusedVisibility(const ReSTIRDIReservoir reservoir, const uint maxAge, const float maxDistance, out float visibility)
{
    if (reservoir.Age > 0u && reservoir.Age <= maxAge && length(float2(reservoir.SpatialDistance)) < maxDistance)
    {
        visibility = float(reservoir.PackedVisibility & ReSTIRDIVisibilityChannelMask) / float(ReSTIRDIVisibilityChannelMask);
        return true;
    }

    visibility = 0.0f;
    return false;
}

bool ReSTIRDIStreamSample(
    inout ReSTIRDIReservoir reservoir,
    const uint lightIndex,
    const float2 sampleUv,
    const float candidateTargetPdf,
    const float inverseSourcePdf,
    const float randomSample)
{
    const float candidateWeight = candidateTargetPdf * inverseSourcePdf;
    reservoir.M += 1.0f;
    reservoir.WeightSum += candidateWeight;
    const bool selectSample = randomSample * reservoir.WeightSum < candidateWeight;
    if (selectSample)
    {
        reservoir.LightData = lightIndex | ReSTIRDILightValidBit;
        reservoir.UvData = ReSTIRDIEncodeSampleUv(sampleUv);
        reservoir.SelectedTargetPdf = candidateTargetPdf;
    }
    return selectSample;
}

bool ReSTIRDIInternalSimpleResample(
    inout ReSTIRDIReservoir reservoir,
    const ReSTIRDIReservoir sourceReservoir,
    const float randomSample,
    const float targetPdfAtReceiver,
    const float sampleNormalization,
    const float sampleM)
{
    const float candidateWeight = targetPdfAtReceiver * sampleNormalization;
    reservoir.M += sampleM;
    reservoir.WeightSum += candidateWeight;
    const bool selectSample = randomSample * reservoir.WeightSum < candidateWeight;
    if (selectSample)
    {
        reservoir.LightData = sourceReservoir.LightData;
        reservoir.UvData = sourceReservoir.UvData;
        reservoir.SelectedTargetPdf = targetPdfAtReceiver;
        reservoir.PackedVisibility = sourceReservoir.PackedVisibility;
        reservoir.SpatialDistance = sourceReservoir.SpatialDistance;
        reservoir.Age = sourceReservoir.Age;
    }
    return selectSample;
}

bool ReSTIRDICombineReservoirs(
    inout ReSTIRDIReservoir reservoir,
    const ReSTIRDIReservoir sourceReservoir,
    const float randomSample,
    const float targetPdfAtReceiver)
{
    return ReSTIRDIInternalSimpleResample(
        reservoir,
        sourceReservoir,
        randomSample,
        targetPdfAtReceiver,
        sourceReservoir.WeightSum * sourceReservoir.M,
        sourceReservoir.M);
}

void ReSTIRDIFinalizeResampling(inout ReSTIRDIReservoir reservoir)
{
    const float normalizationDenominator = reservoir.SelectedTargetPdf * reservoir.M;
    reservoir.WeightSum = normalizationDenominator > 0.0f
        ? reservoir.WeightSum / normalizationDenominator
        : 0.0f;
}

void ReSTIRDIFinalizeResampling(
    inout ReSTIRDIReservoir reservoir,
    const float normalizationNumerator,
    const float normalizationDenominator)
{
    const float denominator = reservoir.SelectedTargetPdf * normalizationDenominator;
    reservoir.WeightSum = denominator > 0.0f
        ? reservoir.WeightSum * normalizationNumerator / denominator
        : 0.0f;
}

float ReSTIRDIGetFinalWeight(const ReSTIRDIReservoir reservoir)
{
    return ReSTIRDIIsValid(reservoir) ? reservoir.WeightSum : 0.0f;
}

bool ReSTIRDIIsSurfaceCompatible(
    const float3 receiverNormal,
    const float receiverDepth,
    const float3 sourceNormal,
    const float sourceDepth,
    const float normalThreshold,
    const float depthThreshold)
{
    const float depthDifference = abs(receiverDepth - sourceDepth);
    const float normalizedDepthThreshold = depthThreshold * max(receiverDepth, sourceDepth);
    return dot(receiverNormal, sourceNormal) >= normalThreshold && depthDifference <= normalizedDepthThreshold;
}

float ReSTIRDIPairwiseMFactor(const float q0, const float q1)
{
    return q0 <= 0.0f ? 1.0f : clamp(pow(min(q1 / q0, 1.0f), 8.0f), 0.0f, 1.0f);
}

float ReSTIRDIPairwiseMisWeight(const float w0, const float w1, const float m0, const float m1)
{
    const float denominator = m0 * w0 + m1 * w1;
    return denominator <= 0.0f ? 0.0f : max(0.0f, m0 * w0) / denominator;
}

uint4 ReSTIRDIPackReservoirCore(const ReSTIRDIReservoir reservoir)
{
    return uint4(
        reservoir.LightData,
        reservoir.UvData,
        asuint(reservoir.SelectedTargetPdf),
        asuint(reservoir.WeightSum));
}

uint4 ReSTIRDIPackReservoirState(const ReSTIRDIReservoir reservoir)
{
    const int2 distance = clamp(reservoir.SpatialDistance, int2(-127, -127), int2(127, 127));
    const uint distanceAge = (uint(distance.x) & 0xffu) |
        ((uint(distance.y) & 0xffu) << 8u) |
        (min(reservoir.Age, 255u) << 16u);
    return uint4(
        reservoir.PackedVisibility | (min(uint(max(reservoir.M, 0.0f)), ReSTIRDIMaxM) << ReSTIRDIMShift),
        distanceAge,
        0u,
        0u);
}

ReSTIRDIReservoir ReSTIRDIUnpackReservoir(const uint4 packedCore, const uint4 packedState)
{
    ReSTIRDIReservoir reservoir;
    reservoir.LightData = packedCore.x;
    reservoir.UvData = packedCore.y;
    reservoir.SelectedTargetPdf = asfloat(packedCore.z);
    reservoir.WeightSum = asfloat(packedCore.w);
    reservoir.M = float((packedState.x >> ReSTIRDIMShift) & ReSTIRDIMaxM);
    reservoir.PackedVisibility = packedState.x & ReSTIRDIVisibilityMask;
    reservoir.SpatialDistance.x = int(packedState.y << 24u) >> 24;
    reservoir.SpatialDistance.y = int(packedState.y << 16u) >> 24;
    reservoir.Age = (packedState.y >> 16u) & 0xffu;
    reservoir.CanonicalWeight = 0.0f;
    if (!isfinite(reservoir.WeightSum) || !isfinite(reservoir.SelectedTargetPdf))
    {
        return ReSTIRDIEmptyReservoir();
    }
    return reservoir;
}
//Modify End

#endif
