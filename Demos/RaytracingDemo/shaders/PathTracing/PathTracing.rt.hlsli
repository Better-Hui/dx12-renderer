#ifndef RAYTRACING_DEMO_PATH_TRACING_DXR_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_DXR_HLSLI

#include "PathTracingGeometry.hlsli"

RayPayload TraceScene(float3 origin, float3 direction, float tMax, uint flags)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
//Modify Begin:2026-07-30 by BestHui
    ray.TMin = 0.0001f;
//Modify End
    ray.TMax = tMax;

    RayPayload payload;
    payload.BaseColor = 0.0f;
    payload.HitT = 0.0f;
    payload.Normal = 0.0f;
    payload.Hit = 0u;
    payload.PositionError = 0.0f;
    payload.Metallic = 0.0f;
    payload.Roughness = 1.0f;
    payload.AmbientOcclusion = 1.0f;
    payload.Padding0 = 0u;

//Modify Begin:2026-07-27 by BestHui
    const uint hitGroupIndex = (flags & RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) != 0u ? 1u : 0u;
    TraceRay(RAYTRACING_DEMO_SCENE, flags, 0xFF, hitGroupIndex, 0, 0, ray, payload);
//Modify End
    return payload;
}

#endif
