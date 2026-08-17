#ifndef RAYTRACING_DEMO_GBUFFER_SAMPLING_HLSLI
#define RAYTRACING_DEMO_GBUFFER_SAMPLING_HLSLI

#include "../Common/RayOffset.hlsli"
#include "../PathTracing/PathTracingSurface.hlsli"
#include "../Scene/SceneResources.hlsli"

float3 DecodeNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}

//Modify Begin:2026-08-06 by Hui
SurfaceData LoadGBufferSurface(uint2 pixel)
{
    SurfaceData surface;
    surface.Diffuse = 0.0f;
    surface.Specular = 0.0f;
    surface.PositionWs = 0.0f;
    surface.NormalWs = float3(0.0f, 1.0f, 0.0f);
    surface.PositionError = 0.0f;
    surface.Metallic = 0.0f;
    surface.Roughness = 1.0f;
    surface.AmbientOcclusion = 1.0f;
    surface.Valid = false;

    float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth >= 1.0f)
    {
        return surface;
    }

    float4 albedoOcclusion = GBufferTextures[GBuffer_AlbedoOcclusion].Load(int3(pixel, 0));
    float4 specularSmoothness = GBufferTextures[GBuffer_SpecularSmoothness].Load(int3(pixel, 0));
    float4 normal = GBufferTextures[GBuffer_Normal].Load(int3(pixel, 0));
    float4 emissionMetallic = GBufferTextures[GBuffer_EmissionMetallic].Load(int3(pixel, 0));
    float4 position = GBufferTextures[GBuffer_Position].Load(int3(pixel, 0));

    surface.Diffuse = saturate(albedoOcclusion.rgb);
    surface.Specular = saturate(specularSmoothness.rgb);
    surface.Roughness = 1.0f - saturate(specularSmoothness.a);
    surface.PositionWs = position.xyz;
//Modify Begin:2026-08-12 by Hui
    surface.NormalWs = DecodeNormal(normal.xyz);
//Modify End
    surface.PositionError = ComputePositionError(surface.PositionWs);
    surface.Metallic = saturate(emissionMetallic.a);
    surface.AmbientOcclusion = saturate(albedoOcclusion.a);
    surface.Valid = true;
    return surface;
}

#endif
