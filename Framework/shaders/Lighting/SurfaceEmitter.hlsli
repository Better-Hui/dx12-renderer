#ifndef FRAMEWORK_SURFACE_EMITTER_HLSLI
#define FRAMEWORK_SURFACE_EMITTER_HLSLI

//Modify Begin:2026-08-06 by Hui
static const uint SurfaceEmitterInvalidMaterialIndex = 0xffffffffu;
static const uint SurfaceEmitterInstanceFlagUseMaterialEmission = 1u << 0u;

struct SurfaceEmitterGeometryData
{
    uint TriangleOffset;
    uint TriangleCount;
    uint TriangleCdfOffset;
    uint Reserved;
};

struct SurfaceEmitterTriangleData
{
    float4 Position0;
    float4 Position1;
    float4 Position2;
    float4 Uv0Uv1;
    float4 Uv2AndPadding;
};

struct SurfaceEmitterInstanceData
{
    float4 OriginAndRange;
    float4 AxisX;
    float4 AxisY;
    float4 AxisZ;
    float4 EmissionAndIntensity;
    uint GeometryIndex;
    uint MaterialIndex;
    uint Flags;
    float SurfaceArea;
};
//Modify End

#endif
