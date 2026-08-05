#ifndef FRAMEWORK_RESTIR_DI_HLSLI
#define FRAMEWORK_RESTIR_DI_HLSLI

//Modify Begin:2026-08-05 by BestHui
struct ReSTIRDIReservoir
{
    uint LightIndex;
    uint SampleSeed;
    float WeightSum;
    float SelectedTargetPdf;
    float M;
    uint Age;
    float CanonicalWeight;
};

ReSTIRDIReservoir ReSTIRDIEmptyReservoir()
{
    ReSTIRDIReservoir reservoir;
    reservoir.LightIndex = 0u;
    reservoir.SampleSeed = 0u;
    reservoir.WeightSum = 0.0f;
    reservoir.SelectedTargetPdf = 0.0f;
    reservoir.M = 0.0f;
    reservoir.Age = 0u;
    reservoir.CanonicalWeight = 0.0f;
    return reservoir;
}

bool ReSTIRDIIsValid(const ReSTIRDIReservoir reservoir)
{
    return reservoir.M > 0.0f && reservoir.WeightSum > 0.0f && reservoir.SelectedTargetPdf > 0.0f;
}

static const uint ReSTIRDIVisibilityStageCandidate = 1u << 0u;
static const uint ReSTIRDIVisibilityStageTemporal = 1u << 1u;
static const uint ReSTIRDIVisibilityStageSpatial = 1u << 2u;
static const uint ReSTIRDIVisibilityStageFinal = 1u << 3u;

bool ReSTIRDIShouldTestVisibility(const uint visibilityTestMask, const uint stage)
{
    return (visibilityTestMask & stage) != 0u;
}

bool ReSTIRDIStreamSample(
    inout ReSTIRDIReservoir reservoir,
    const uint lightIndex,
    const uint sampleSeed,
    const float candidateWeight,
    const float candidateTargetPdf,
    const float candidateM,
    const uint candidateAge,
    const float randomSample)
{
    reservoir.M = min(1048575.0f, reservoir.M + candidateM);
    if (candidateWeight <= 0.0f || candidateTargetPdf <= 0.0f)
    {
        return false;
    }

    const float combinedWeight = reservoir.WeightSum + candidateWeight;
    if (randomSample * combinedWeight < candidateWeight)
    {
        reservoir.LightIndex = lightIndex;
        reservoir.SampleSeed = sampleSeed;
        reservoir.SelectedTargetPdf = candidateTargetPdf;
        reservoir.Age = candidateAge;
    }

    reservoir.WeightSum = combinedWeight;
    return randomSample * combinedWeight < candidateWeight;
}

bool ReSTIRDICombineReservoirs(
    inout ReSTIRDIReservoir reservoir,
    const ReSTIRDIReservoir sourceReservoir,
    const float targetPdfAtReceiver,
    const float randomSample)
{
    if (sourceReservoir.M <= 0.0f)
    {
        return false;
    }

    if (!ReSTIRDIIsValid(sourceReservoir) || targetPdfAtReceiver <= 0.0f)
    {
        reservoir.M = min(1048575.0f, reservoir.M + sourceReservoir.M);
        return false;
    }

    return ReSTIRDIStreamSample(
        reservoir,
        sourceReservoir.LightIndex,
        sourceReservoir.SampleSeed,
        sourceReservoir.WeightSum * sourceReservoir.M * targetPdfAtReceiver,
        targetPdfAtReceiver,
        sourceReservoir.M,
        sourceReservoir.Age,
        randomSample);
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
    return dot(receiverNormal, sourceNormal) >= normalThreshold &&
        depthDifference <= normalizedDepthThreshold;
}

float ReSTIRDIClampHistoryM(const ReSTIRDIReservoir reservoir, const uint maxHistoryLength)
{
    return min(reservoir.M, max(1.0f, float(maxHistoryLength)));
}

uint4 ReSTIRDIPackReservoir(const ReSTIRDIReservoir reservoir)
{
    return uint4(
        (reservoir.LightIndex & 0x0000ffffu) | ((reservoir.SampleSeed & 0x0000ffffu) << 16u),
        asuint(reservoir.WeightSum),
        asuint(reservoir.SelectedTargetPdf),
        asuint(reservoir.M));
}

ReSTIRDIReservoir ReSTIRDIUnpackReservoir(const uint4 packedReservoir)
{
    ReSTIRDIReservoir reservoir;
    reservoir.LightIndex = packedReservoir.x & 0x0000ffffu;
    reservoir.SampleSeed = packedReservoir.x >> 16u;
    reservoir.WeightSum = asfloat(packedReservoir.y);
    reservoir.SelectedTargetPdf = asfloat(packedReservoir.z);
    reservoir.M = asfloat(packedReservoir.w);
    reservoir.Age = 0u;
    reservoir.CanonicalWeight = 0.0f;
    return reservoir;
}
//Modify End

#endif
