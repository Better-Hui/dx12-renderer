#ifndef RAYTRACING_DEMO_SCENE_RESOURCES_HLSLI
#define RAYTRACING_DEMO_SCENE_RESOURCES_HLSLI

#if !defined(RAYTRACING_DEMO_INLINE_BACKEND)
#define RAYTRACING_DEMO_INLINE_BACKEND 0
#endif

#if !defined(RAYTRACING_DEMO_COMPACTED_DISPATCH)
#define RAYTRACING_DEMO_COMPACTED_DISPATCH 0
#endif

//Modify Begin:2026-08-06 by Hui
#if !defined(RAYTRACING_DEMO_ENVIRONMENT_PROJECTION)
#define RAYTRACING_DEMO_ENVIRONMENT_PROJECTION 0
#endif
//Modify End

#include "../GBuffer/GBufferLayout.hlsli"
#include <Bindless/BindlessResources.hlsli>
//Modify Begin:2026-08-11 by Hui
#include <Common/EnvironmentTexture.hlsli>
//Modify End
#include <Lighting/SurfaceEmitter.hlsli>
#include "SceneCamera.hlsli"
#include "SceneGeometryBindless.hlsli"
#include "SceneLighting.hlsli"

#if RAYTRACING_DEMO_INLINE_BACKEND
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <ShaderLibrary/RayTracing/InlineRayTracing.hlsli>
#endif

#if RAYTRACING_DEMO_INLINE_BACKEND

#define RAYTRACING_DEMO_SCENE g_InlineRayTracingScene
#define RAYTRACING_DEMO_GBUFFER_REGISTER register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_DEPTH_REGISTER register(t5, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_SKYBOX_REGISTER register(t6, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_MATERIALS_REGISTER register(t7, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_GEOMETRIES_REGISTER register(t8, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_DIRECTIONAL_LIGHTS_REGISTER register(t9, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_POINT_LIGHTS_REGISTER register(t10, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
//Modify Begin:2026-08-26 by Hui
#define RAYTRACING_DEMO_SPOT_LIGHTS_REGISTER register(t11, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
//Modify End
#define RAYTRACING_DEMO_SURFACE_EMITTER_GEOMETRIES_REGISTER register(t21, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_SURFACE_EMITTER_INSTANCES_REGISTER register(t22, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLES_REGISTER register(t23, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLE_CDF_REGISTER register(t24, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_DIRECT_LIGHT_CDF_REGISTER register(t25, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_ACTIVE_PIXEL_INDICES_REGISTER register(t26, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_ACTIVE_PIXEL_COUNT_REGISTER register(t27, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
#define RAYTRACING_DEMO_DIRECT_LIGHTING_REGISTER register(u0)
#define RAYTRACING_DEMO_INDIRECT_LIGHTING_REGISTER register(u1)
#define RAYTRACING_DEMO_LINEAR_SAMPLER_REGISTER register(s1)

#else

#define RAYTRACING_DEMO_SCENE Scene
#define RAYTRACING_DEMO_GBUFFER_REGISTER register(t0, space4)
#define RAYTRACING_DEMO_DEPTH_REGISTER register(t0, space6)
#define RAYTRACING_DEMO_SKYBOX_REGISTER register(t0, space5)
#define RAYTRACING_DEMO_MATERIALS_REGISTER register(t1, space0)
#define RAYTRACING_DEMO_GEOMETRIES_REGISTER register(t2, space0)
#define RAYTRACING_DEMO_DIRECTIONAL_LIGHTS_REGISTER register(t3, space0)
#define RAYTRACING_DEMO_POINT_LIGHTS_REGISTER register(t4, space0)
//Modify Begin:2026-08-26 by Hui
#define RAYTRACING_DEMO_SPOT_LIGHTS_REGISTER register(t11, space0)
//Modify End
#define RAYTRACING_DEMO_SURFACE_EMITTER_GEOMETRIES_REGISTER register(t5, space0)
#define RAYTRACING_DEMO_SURFACE_EMITTER_INSTANCES_REGISTER register(t6, space0)
#define RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLES_REGISTER register(t7, space0)
#define RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLE_CDF_REGISTER register(t8, space0)
#define RAYTRACING_DEMO_DIRECT_LIGHT_CDF_REGISTER register(t9, space0)
#define RAYTRACING_DEMO_ACTIVE_PIXEL_INDICES_REGISTER register(t10, space0)
#define RAYTRACING_DEMO_DIRECT_LIGHTING_REGISTER register(u0, space0)
#define RAYTRACING_DEMO_INDIRECT_LIGHTING_REGISTER register(u1, space0)
#define RAYTRACING_DEMO_LINEAR_SAMPLER_REGISTER register(s0)

RaytracingAccelerationStructure Scene : register(t0, space0);

#endif

Texture2D<float4> GBufferTextures
    [GBuffer_Count]
    : RAYTRACING_DEMO_GBUFFER_REGISTER;

Texture2D<float> DepthTexture : RAYTRACING_DEMO_DEPTH_REGISTER;
//Modify Begin:2026-08-06 by Hui
#if RAYTRACING_DEMO_ENVIRONMENT_PROJECTION != 0
Texture2D Skybox : RAYTRACING_DEMO_SKYBOX_REGISTER;
#else
TextureCube Skybox : RAYTRACING_DEMO_SKYBOX_REGISTER;
#endif
//Modify End
StructuredBuffer<MaterialData> Materials : RAYTRACING_DEMO_MATERIALS_REGISTER;
StructuredBuffer<GeometryData> Geometries : RAYTRACING_DEMO_GEOMETRIES_REGISTER;
StructuredBuffer<DirectionalLightData> DirectionalLights : RAYTRACING_DEMO_DIRECTIONAL_LIGHTS_REGISTER;
StructuredBuffer<PointLightData> PointLights : RAYTRACING_DEMO_POINT_LIGHTS_REGISTER;
//Modify Begin:2026-08-26 by Hui
StructuredBuffer<SpotLightData> SpotLights : RAYTRACING_DEMO_SPOT_LIGHTS_REGISTER;
//Modify End
StructuredBuffer<SurfaceEmitterGeometryData> SurfaceEmitterGeometries : RAYTRACING_DEMO_SURFACE_EMITTER_GEOMETRIES_REGISTER;
StructuredBuffer<SurfaceEmitterInstanceData> SurfaceEmitterInstances : RAYTRACING_DEMO_SURFACE_EMITTER_INSTANCES_REGISTER;
StructuredBuffer<SurfaceEmitterTriangleData> SurfaceEmitterTriangles : RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLES_REGISTER;
StructuredBuffer<float> SurfaceEmitterTriangleCdf : RAYTRACING_DEMO_SURFACE_EMITTER_TRIANGLE_CDF_REGISTER;
StructuredBuffer<float> DirectLightCdf : RAYTRACING_DEMO_DIRECT_LIGHT_CDF_REGISTER;

#if RAYTRACING_DEMO_COMPACTED_DISPATCH
StructuredBuffer<uint> ActiveRayPixelIndices : RAYTRACING_DEMO_ACTIVE_PIXEL_INDICES_REGISTER;
#if RAYTRACING_DEMO_INLINE_BACKEND
ByteAddressBuffer ActiveRayPixelCount : RAYTRACING_DEMO_ACTIVE_PIXEL_COUNT_REGISTER;
#endif
#endif

RWTexture2D<float4> DirectLighting : RAYTRACING_DEMO_DIRECT_LIGHTING_REGISTER;
RWTexture2D<float4> IndirectLighting : RAYTRACING_DEMO_INDIRECT_LIGHTING_REGISTER;
SamplerState LinearWrapSampler : RAYTRACING_DEMO_LINEAR_SAMPLER_REGISTER;

//Modify Begin:2026-08-11 by Hui
float3 SampleEnvironmentRadiance(const float3 directionWs)
{
    float3 textureRadiance = float3(1.0f, 1.0f, 1.0f);
    if (Camera_UseSolidSkyFallback == 0u)
    {
#if RAYTRACING_DEMO_ENVIRONMENT_PROJECTION == 1
    const float2 uv = float2(
        atan2(directionWs.z, directionWs.x) / 6.28318530718f + 0.5f,
        acos(clamp(directionWs.y, -1.0f, 1.0f)) / 3.14159265359f);
        textureRadiance = Skybox.SampleLevel(LinearWrapSampler, uv, 0.0f).rgb;
#elif RAYTRACING_DEMO_ENVIRONMENT_PROJECTION == 2
        textureRadiance = Skybox.SampleLevel(
            LinearWrapSampler,
            FrameworkDirectionToHorizontalCubemapStripUv(directionWs),
            0.0f).rgb;
#else
        textureRadiance = Skybox.SampleLevel(LinearWrapSampler, directionWs, 0.0f).rgb;
#endif
    }
    return textureRadiance *
        Camera_SkyLight.ColorAndIntensity.rgb *
        Camera_SkyLight.ColorAndIntensity.w;
}
//Modify End

#endif
