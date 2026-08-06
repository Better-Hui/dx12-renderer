#ifndef RAYTRACING_DEMO_PATH_TRACING_PAYLOAD_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_PAYLOAD_HLSLI

#include "../Scene/SceneResources.hlsli"

#if !defined(RAYTRACING_DEMO_USE_ANNOTATED_PAYLOAD)
#if defined(__SHADER_TARGET_MAJOR) && defined(__SHADER_TARGET_MINOR) && (__SHADER_TARGET_MAJOR > 6 || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 10))
#define RAYTRACING_DEMO_USE_ANNOTATED_PAYLOAD 1
#else
#define RAYTRACING_DEMO_USE_ANNOTATED_PAYLOAD 0
#endif
#endif

#if !RAYTRACING_DEMO_INLINE_BACKEND && RAYTRACING_DEMO_USE_ANNOTATED_PAYLOAD
struct [raypayload] RayPayload
{
    float3 BaseColor : read(caller) : write(caller, closesthit, miss);
    float HitT : read(caller) : write(caller, closesthit, miss);
    float3 Normal : read(caller) : write(caller, closesthit, miss);
    uint Hit : read(caller) : write(caller, closesthit, miss);
    float3 PositionError : read(caller) : write(caller, closesthit, miss);
    float Metallic : read(caller) : write(caller, closesthit, miss);
    float Roughness : read(caller) : write(caller, closesthit, miss);
    float AmbientOcclusion : read(caller) : write(caller, closesthit, miss);
//Modify Begin:2026-08-06 by BestHui
    float3 Emission : read(caller) : write(caller, closesthit, miss);
    uint Padding0 : read(caller) : write(caller, closesthit, miss);
//Modify End
};
#else
struct RayPayload
{
    float3 BaseColor;
    float HitT;
    float3 Normal;
    uint Hit;
    float3 PositionError;
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
//Modify Begin:2026-08-06 by BestHui
    float3 Emission;
//Modify End
    uint Padding0;
};
#endif

#endif
