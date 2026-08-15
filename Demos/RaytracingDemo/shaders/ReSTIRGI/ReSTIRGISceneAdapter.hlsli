#ifndef RAYTRACING_DEMO_RESTIR_GI_SCENE_ADAPTER_HLSLI
#define RAYTRACING_DEMO_RESTIR_GI_SCENE_ADAPTER_HLSLI

//Modify Begin:2026-08-10 by BestHui
#define FRAMEWORK_RESTIR_GI_SCENE_ADAPTER 1

//Modify Begin:2026-08-11 by BestHui
#include <ReSTIRGI/ReSTIRGIConstants.hlsli>
#define RAYTRACING_DEMO_MAX_BOUNCES RESTIR_GI_MAX_PATH_BOUNCES
//Modify End
#include "../PathTracing/PathTracing.rayquery.hlsli"
#include "../PathTracing/PathTracingShared.hlsli"
#include <ReSTIRGI/ReSTIRGI.hlsli>

float3 RaytracingDemoReSTIRGIEvaluateContribution(
    const SurfaceData surface,
    const ReSTIRGIReservoir reservoir)
{
//Modify Begin:2026-07-30 by BestHui
    if (!surface.Valid || !ReSTIRGIIsValid(reservoir))
    {
        return 0.0f;
    }

    const float3 toSample = reservoir.SamplePosition - surface.PositionWs;
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 0.000001f)
    {
        return 0.0f;
    }

    const float3 directionWs = toSample * rsqrt(distanceSquared);
    return EvaluateDiffuseBounceContribution(surface, directionWs, reservoir.Radiance);
//Modify End
}

bool RaytracingDemoReSTIRGIGenerateInitialSample(
    const SurfaceData surface,
    const uint2 pixel,
    inout uint randomState,
    const float2 directionalSample,
    out ReSTIRGIReservoir reservoir)
{
//Modify Begin:2026-07-30 by BestHui
    reservoir = ReSTIRGIEmptyReservoir();
    float3 directionWs = 0.0f;
    float sourcePdf = 0.0f;
    if (!SampleDiffuseDirection(
        surface,
        directionalSample,
        directionWs,
        sourcePdf))
    {
        return false;
    }

    const float3 rayOrigin = OffsetRayOrigin(
        surface.PositionWs,
        surface.PositionError,
        surface.NormalWs,
        directionWs);
    const RayPayload payload = TraceScene(rayOrigin, directionWs, 10000.0f, RAY_FLAG_NONE);
    reservoir.CreationPosition = surface.PositionWs;
    reservoir.CreationNormal = surface.NormalWs;
    reservoir.M = 1u;
    reservoir.AverageWeight = rcp(max(0.000001f, sourcePdf));
    reservoir.AgeAndFlags = 0u;
    ReSTIRGISetCreationVisibility(reservoir, true);

    if (payload.Hit == 0u)
    {
        reservoir.SamplePosition = rayOrigin + directionWs * 10000.0f;
        reservoir.SampleNormal = -directionWs;
        reservoir.Radiance = payload.BaseColor;
        ReSTIRGISetEnvironmentSample(reservoir, true);
        return true;
    }

    const float3 hitPosition = rayOrigin + directionWs * payload.HitT;
    const SurfaceData hitSurface = MakeRayHitSurface(payload, hitPosition);
    reservoir.SamplePosition = hitPosition;
    reservoir.SampleNormal = payload.Normal;
    reservoir.Radiance = TraceDiffuseGatherPathRadiance(
        hitSurface,
        payload.Emission,
        randomState);
    return true;
//Modify End
}

bool RaytracingDemoReSTIRGITestVisibility(
    const SurfaceData surface,
    const ReSTIRGIReservoir reservoir)
{
    if (!surface.Valid || !ReSTIRGIIsValid(reservoir))
    {
        return false;
    }

    const float3 toSample = reservoir.SamplePosition - surface.PositionWs;
    const float distanceToSample = length(toSample);
    if (distanceToSample <= 0.001f)
    {
        return false;
    }

    const float3 directionWs = toSample / distanceToSample;
    const float3 rayOrigin = OffsetRayOrigin(
        surface.PositionWs,
        surface.PositionError,
        surface.NormalWs,
        directionWs);
    const float tMax = ReSTIRGIIsEnvironmentSample(reservoir)
        ? 10000.0f
        : max(0.001f, distanceToSample - 0.01f);
    return IsVisibleAlongRay(rayOrigin, directionWs, tMax);
}

//Modify Begin:2026-07-30 by BestHui
bool RaytracingDemoReSTIRGITestVisibilityAt(
    const float3 originPositionWs,
    const float3 originNormalWs,
    const ReSTIRGIReservoir reservoir)
{
    if (!ReSTIRGIIsValid(reservoir))
    {
        return false;
    }

    const float3 toSample = reservoir.SamplePosition - originPositionWs;
    const float distanceToSample = length(toSample);
    if (distanceToSample <= 0.001f)
    {
        return false;
    }

    const float3 directionWs = toSample / distanceToSample;
    if (dot(originNormalWs, directionWs) <= 0.0f)
    {
        return false;
    }

    const float3 rayOrigin = OffsetRayOrigin(
        originPositionWs,
        ComputePositionError(originPositionWs),
        originNormalWs,
        directionWs);
    const float tMax = ReSTIRGIIsEnvironmentSample(reservoir)
        ? 10000.0f
        : max(0.001f, distanceToSample - 0.01f);
    return IsVisibleAlongRay(rayOrigin, directionWs, tMax);
}
//Modify End

#define ReSTIRGI_Surface SurfaceData
#define ReSTIRGI_LoadSurface LoadGBufferSurface
#define ReSTIRGI_LoadMotionVector(pixel) MotionVectorTexture.Load(int3((pixel), 0))
#define ReSTIRGI_GenerateInitialSample RaytracingDemoReSTIRGIGenerateInitialSample
#define ReSTIRGI_EvaluateContribution RaytracingDemoReSTIRGIEvaluateContribution
#define ReSTIRGI_TestVisibility RaytracingDemoReSTIRGITestVisibility
//Modify Begin:2026-07-30 by BestHui
#define ReSTIRGI_TestVisibilityAt RaytracingDemoReSTIRGITestVisibilityAt
//Modify End
#define ReSTIRGI_IndirectLighting IndirectLighting
#define ReSTIRGI_Luminance Luminance
#define ReSTIRGI_Random01 Random01
#define ReSTIRGI_InitializeRandomState InitializeRandomState
// Keep GI on the shared IGN + random-rotation sequence without its diagonal
// temporal coordinate shift; mix the frame into the salt instead.
#define ReSTIRGI_SampleNoise(pixel, frameIndex, salt) \
    FrameworkInterleavedGradientNoise2D((pixel), 0u, (salt) ^ ((frameIndex) * 0x9e3779b9u))
//Modify End

#endif
