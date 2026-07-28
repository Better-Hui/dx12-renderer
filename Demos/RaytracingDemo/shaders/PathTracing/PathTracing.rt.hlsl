#define RAYTRACING_DEMO_ENABLE_LINALG 0

#if defined(RAYTRACING_DEMO_ENABLE_LINALG) && RAYTRACING_DEMO_ENABLE_LINALG
#include <hlsl/dx/linalg.h>
#endif
#include "PathTracing.rt.hlsli"

float3 ApplyClosestHitCooperativeVector(float3 color)
{
#if defined(RAYTRACING_DEMO_ENABLE_LINALG) && RAYTRACING_DEMO_ENABLE_LINALG
    uint3 quantizedColor = uint3(saturate(color) * 1024.0f + 0.5f);
    dx::linalg::InterpretedVector<float, 3, dx::linalg::ComponentType::F32> convertedColor =
        dx::linalg::Convert<dx::linalg::ComponentType::F32, dx::linalg::ComponentType::U32>(quantizedColor);
    return saturate(float3(convertedColor.Data[0], convertedColor.Data[1], convertedColor.Data[2]) / 1024.0f);
#else
    return color;
#endif
}

#include "PathTracingShared.hlsli"

[shader("raygeneration")]
void DirectLightingRayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    WriteDirectLightingOutput(pixel, Camera_Width, Camera_FrameIndex);
}

[shader("raygeneration")]
void RayGen()
{
    DirectLightingRayGen();
}

[shader("raygeneration")]
void IndirectLightingRayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    WriteIndirectLightingOutput(pixel, Camera_Width, Camera_FrameIndex);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload = MakeMissPayload(WorldRayDirection());
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    GeometryData geometry = Geometries[InstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];
    payload = MakeTrianglePayload(
        geometry,
        material,
        PrimitiveIndex(),
        attributes.barycentrics,
        WorldRayDirection(),
        ObjectToWorld3x4(),
        RayTCurrent());
    payload.BaseColor = ApplyClosestHitCooperativeVector(payload.BaseColor);
}

//Modify Begin:2026-07-27 by BestHui
[shader("closesthit")]
void VisibilityClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    (void)attributes;
    payload.Hit = 1u;
    payload.HitT = RayTCurrent();
}
//Modify End
