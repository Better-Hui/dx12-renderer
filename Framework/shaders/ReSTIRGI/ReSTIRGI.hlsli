#ifndef FRAMEWORK_RESTIR_GI_HLSLI
#define FRAMEWORK_RESTIR_GI_HLSLI

//Modify Begin:2026-08-10 by Hui
static const uint ReSTIRGIEnvironmentSampleBit = 0x80000000u;
static const uint ReSTIRGICreationVisibilityKnownBit = 0x40000000u;
static const uint ReSTIRGIAgeMask = 0x3fffffffu;

struct ReSTIRGIReservoir
{
    float3 CreationPosition;
    float3 CreationNormal;
    float3 SamplePosition;
    float3 SampleNormal;
    float3 Radiance;
    uint M;
    float AverageWeight;
    uint AgeAndFlags;
};

float2 ReSTIRGISignNotZero(const float2 value)
{
    return float2(value.x >= 0.0f ? 1.0f : -1.0f, value.y >= 0.0f ? 1.0f : -1.0f);
}

uint ReSTIRGIPackNormal(const float3 normal)
{
    float3 unitNormal = normalize(normal);
    float2 encoded = unitNormal.xy / max(0.000001f, abs(unitNormal.x) + abs(unitNormal.y) + abs(unitNormal.z));
    if (unitNormal.z < 0.0f)
    {
        encoded = (1.0f - abs(encoded.yx)) * ReSTIRGISignNotZero(encoded);
    }

    const uint2 packed = uint2(round(saturate(encoded * 0.5f + 0.5f) * 65535.0f));
    return packed.x | (packed.y << 16u);
}

float3 ReSTIRGIUnpackNormal(const uint packed)
{
    float2 encoded = float2(packed & 0xffffu, packed >> 16u) / 65535.0f * 2.0f - 1.0f;
    float3 normal = float3(encoded, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (normal.z < 0.0f)
    {
        normal.xy = (1.0f - abs(normal.yx)) * ReSTIRGISignNotZero(normal.xy);
    }
    return normalize(normal);
}

ReSTIRGIReservoir ReSTIRGIEmptyReservoir()
{
    ReSTIRGIReservoir reservoir;
    reservoir.CreationPosition = 0.0f;
    reservoir.CreationNormal = float3(0.0f, 1.0f, 0.0f);
    reservoir.SamplePosition = 0.0f;
    reservoir.SampleNormal = float3(0.0f, 1.0f, 0.0f);
    reservoir.Radiance = 0.0f;
    reservoir.M = 0u;
    reservoir.AverageWeight = 0.0f;
    reservoir.AgeAndFlags = 0u;
    return reservoir;
}

bool ReSTIRGIHasCandidates(const ReSTIRGIReservoir reservoir)
{
    return reservoir.M > 0u &&
        all(isfinite(reservoir.CreationPosition)) &&
        all(isfinite(reservoir.SamplePosition)) &&
        all(isfinite(reservoir.Radiance));
}

bool ReSTIRGIIsValid(const ReSTIRGIReservoir reservoir)
{
    return ReSTIRGIHasCandidates(reservoir) &&
        reservoir.AverageWeight > 0.0f &&
        isfinite(reservoir.AverageWeight);
}

bool ReSTIRGIIsEnvironmentSample(const ReSTIRGIReservoir reservoir)
{
    return (reservoir.AgeAndFlags & ReSTIRGIEnvironmentSampleBit) != 0u;
}

bool ReSTIRGIHasCreationVisibility(const ReSTIRGIReservoir reservoir)
{
    return (reservoir.AgeAndFlags & ReSTIRGICreationVisibilityKnownBit) != 0u;
}

uint ReSTIRGIGetAge(const ReSTIRGIReservoir reservoir)
{
    return reservoir.AgeAndFlags & ReSTIRGIAgeMask;
}

void ReSTIRGISetAge(inout ReSTIRGIReservoir reservoir, const uint age)
{
    reservoir.AgeAndFlags = (reservoir.AgeAndFlags &
        (ReSTIRGIEnvironmentSampleBit | ReSTIRGICreationVisibilityKnownBit)) |
        min(age, ReSTIRGIAgeMask);
}

void ReSTIRGISetEnvironmentSample(inout ReSTIRGIReservoir reservoir, const bool isEnvironment)
{
    reservoir.AgeAndFlags = (reservoir.AgeAndFlags &
        (ReSTIRGIAgeMask | ReSTIRGICreationVisibilityKnownBit)) |
        (isEnvironment ? ReSTIRGIEnvironmentSampleBit : 0u);
}

void ReSTIRGISetCreationVisibility(inout ReSTIRGIReservoir reservoir, const bool isKnown)
{
    reservoir.AgeAndFlags = (reservoir.AgeAndFlags &
        (ReSTIRGIEnvironmentSampleBit | ReSTIRGIAgeMask)) |
        (isKnown ? ReSTIRGICreationVisibilityKnownBit : 0u);
}

ReSTIRGIReservoir ReSTIRGIReadReservoir(
    Texture2D<uint4> creationTexture,
    Texture2D<uint4> hitTexture,
    Texture2D<uint4> lightTexture,
    const uint2 pixel)
{
    const uint4 creation = creationTexture.Load(int3(pixel, 0));
    const uint4 hit = hitTexture.Load(int3(pixel, 0));
    const uint4 light = lightTexture.Load(int3(pixel, 0));
    ReSTIRGIReservoir reservoir;
    reservoir.CreationPosition = asfloat(creation.xyz);
    reservoir.CreationNormal = ReSTIRGIUnpackNormal(creation.w);
    reservoir.SamplePosition = asfloat(hit.xyz);
    reservoir.SampleNormal = ReSTIRGIUnpackNormal(hit.w);
    reservoir.Radiance = float3(
        f16tof32(light.x & 0xffffu),
        f16tof32(light.x >> 16u),
        f16tof32(light.y & 0xffffu));
    reservoir.M = light.y >> 16u;
    reservoir.AverageWeight = asfloat(light.z);
    reservoir.AgeAndFlags = light.w;
    if (!ReSTIRGIHasCandidates(reservoir))
    {
        return ReSTIRGIEmptyReservoir();
    }
    return reservoir;
}

void ReSTIRGIWriteReservoir(
    RWTexture2D<uint4> creationTexture,
    RWTexture2D<uint4> hitTexture,
    RWTexture2D<uint4> lightTexture,
    const uint2 pixel,
    const ReSTIRGIReservoir reservoir)
{
    creationTexture[pixel] = uint4(
        asuint(reservoir.CreationPosition),
        ReSTIRGIPackNormal(reservoir.CreationNormal));
    hitTexture[pixel] = uint4(
        asuint(reservoir.SamplePosition),
        ReSTIRGIPackNormal(reservoir.SampleNormal));
    lightTexture[pixel] = uint4(
        f32tof16(reservoir.Radiance.x) | (f32tof16(reservoir.Radiance.y) << 16u),
        f32tof16(reservoir.Radiance.z) | (min(reservoir.M, 0xffffu) << 16u),
        asuint(reservoir.AverageWeight),
        reservoir.AgeAndFlags);
}

void ReSTIRGISelectSample(
    inout ReSTIRGIReservoir destination,
    const ReSTIRGIReservoir source)
{
    destination.CreationPosition = source.CreationPosition;
    destination.CreationNormal = source.CreationNormal;
    destination.SamplePosition = source.SamplePosition;
    destination.SampleNormal = source.SampleNormal;
    destination.Radiance = source.Radiance;
    destination.AverageWeight = source.AverageWeight;
    destination.AgeAndFlags = source.AgeAndFlags;
}

bool ReSTIRGIUpdateReservoir(
    inout ReSTIRGIReservoir destination,
    const ReSTIRGIReservoir source,
    const float candidateWeight,
    const float randomSample,
    inout float weightSum)
{
    if (!ReSTIRGIHasCandidates(source) || !isfinite(candidateWeight))
    {
        return false;
    }

    weightSum += max(0.0f, candidateWeight);
    destination.M += source.M;
    if (candidateWeight > 0.0f && randomSample * weightSum <= candidateWeight)
    {
        ReSTIRGISelectSample(destination, source);
        return true;
    }
    return false;
}

void ReSTIRGISetCreationSurface(
    inout ReSTIRGIReservoir reservoir,
    const float3 creationPosition,
    const float3 creationNormal)
{
    reservoir.CreationPosition = creationPosition;
    reservoir.CreationNormal = creationNormal;
    if (!ReSTIRGIIsValid(reservoir))
    {
        return;
    }

    const float3 inverseDirection = creationPosition - reservoir.SamplePosition;
    if (dot(reservoir.SampleNormal, inverseDirection) < 0.0f)
    {
        reservoir.SampleNormal = -reservoir.SampleNormal;
    }
    const float3 direction = reservoir.SamplePosition - creationPosition;
    if (dot(reservoir.CreationNormal, direction) < 0.0f)
    {
        reservoir.CreationNormal = -reservoir.CreationNormal;
    }
}

float ReSTIRGIComputeJacobian(
    const ReSTIRGIReservoir reservoir,
    const float3 receiverPosition,
    const float3 receiverNormal,
    const float maxJacobian)
{
    if (ReSTIRGIIsEnvironmentSample(reservoir))
    {
        return 1.0f;
    }

    const float3 sourceOffset = reservoir.SamplePosition - reservoir.CreationPosition;
    const float3 receiverOffset = reservoir.SamplePosition - receiverPosition;
    const float sourceDistanceSquared = dot(sourceOffset, sourceOffset);
    const float receiverDistanceSquared = dot(receiverOffset, receiverOffset);
    if (sourceDistanceSquared <= 0.000001f || receiverDistanceSquared <= 0.000001f)
    {
        return 0.0f;
    }

    const float3 sourceDirection = normalize(sourceOffset);
    const float3 receiverDirection = normalize(receiverOffset);
    const float receiverCosine = dot(receiverNormal, receiverDirection);
    const float sourceCosine = dot(reservoir.CreationNormal, sourceDirection);
    const float receiverHitCosine = -dot(receiverDirection, reservoir.SampleNormal);
    const float sourceHitCosine = -dot(sourceDirection, reservoir.SampleNormal);
    if (receiverCosine <= 0.0f || sourceCosine <= 0.0f ||
        receiverHitCosine <= 0.0f || sourceHitCosine <= 0.0f)
    {
        return 0.0f;
    }

    return clamp(
        sourceDistanceSquared * receiverHitCosine /
            max(0.000001f, receiverDistanceSquared * sourceHitCosine),
        0.0f,
        maxJacobian);
}
//Modify End

#endif
