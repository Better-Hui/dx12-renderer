#include "PathTracing.rt.hlsli"

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
//Modify Begin:2026-07-30 by Hui
    const RayPayload missPayload = MakeMissPayload(WorldRayDirection());
    payload.BaseColor = missPayload.BaseColor;
    payload.HitT = missPayload.HitT;
    payload.Normal = missPayload.Normal;
    payload.Hit = missPayload.Hit;
    payload.PositionError = missPayload.PositionError;
    payload.Metallic = missPayload.Metallic;
    payload.Roughness = missPayload.Roughness;
    payload.AmbientOcclusion = missPayload.AmbientOcclusion;
    payload.Emission = missPayload.Emission;
    payload.Padding0 = missPayload.Padding0;
//Modify End
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    GeometryData geometry = Geometries[InstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];
//Modify Begin:2026-07-30 by Hui
    const RayPayload trianglePayload = MakeTrianglePayload(
        geometry,
        material,
        PrimitiveIndex(),
        attributes.barycentrics,
        WorldRayDirection(),
        ObjectToWorld3x4(),
        WorldToObject3x4(),
        RayTCurrent());
    payload.BaseColor = trianglePayload.BaseColor;
    payload.HitT = trianglePayload.HitT;
    payload.Normal = trianglePayload.Normal;
    payload.Hit = trianglePayload.Hit;
    payload.PositionError = trianglePayload.PositionError;
    payload.Metallic = trianglePayload.Metallic;
    payload.Roughness = trianglePayload.Roughness;
    payload.AmbientOcclusion = trianglePayload.AmbientOcclusion;
    payload.Emission = trianglePayload.Emission;
    payload.Padding0 = trianglePayload.Padding0;
//Modify End
}

//Modify Begin:2026-07-27 by Hui
[shader("closesthit")]
void VisibilityClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    (void)attributes;
//Modify Begin:2026-07-30 by Hui
    payload.BaseColor = 0.0f;
    payload.Hit = 1u;
    payload.HitT = RayTCurrent();
    payload.Normal = 0.0f;
    payload.PositionError = 0.0f;
    payload.Metallic = 0.0f;
    payload.Roughness = 1.0f;
    payload.AmbientOcclusion = 1.0f;
    payload.Emission = 0.0f;
    payload.Padding0 = 0u;
//Modify End
}
//Modify End
