#include "../GBuffer/GBufferLayout.hlsli"
#include "../Scene/SceneCamera.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"

Texture2D<float4> GBufferTextures[GBuffer_Count] : register(t0, space0);
Texture2D<float> DepthTexture : register(t5, space0);
Texture2D<float4> DirectLightingTexture : register(t7, space0);
Texture2D<float4> IndirectLightingTexture : register(t8, space0);
RWTexture2D<float4> SceneColor : register(u0, space0);
RWTexture2D<float4> HistoryColor : register(u1, space0);
RWTexture2D<float4> NoisyRadiance : register(u2, space0);
RWTexture2D<float4> NRDNoisyRadiance : register(u3, space0);

float3 SanitizeNRDRadiance(float3 color)
{
    if (!all(isfinite(color)))
    {
        return 0.0f;
    }

    return min(max(color, 0.0f), 250.0f);
}

float3 GetNRDDiffuseDemodulation(uint2 pixel)
{
    const float4 albedoOcclusion = GBufferTextures[GBuffer_AlbedoOcclusion].Load(int3(pixel, 0));
    const float4 emissionMetallic = GBufferTextures[GBuffer_EmissionMetallic].Load(int3(pixel, 0));
    const float metallic = saturate(emissionMetallic.a);
    const float3 diffuseFactor = max(saturate(albedoOcclusion.rgb) * (1.0f - metallic), 0.05f);
    return lerp(diffuseFactor, float3(1.0f, 1.0f, 1.0f), metallic);
}

float GetGBufferRoughness(uint2 pixel)
{
    const float4 specularSmoothness = GBufferTextures[GBuffer_SpecularSmoothness].Load(int3(pixel, 0));
    return 1.0f - saturate(specularSmoothness.a);
}

float GetGBufferViewZ(uint2 pixel)
{
    const float3 positionWs = GBufferTextures[GBuffer_Position].Load(int3(pixel, 0)).xyz;
//Modify Begin:2026-07-28 by BestHui
    const float3 cameraForward = normalize(mul(Camera_InverseView, float4(0.0f, 0.0f, 1.0f, 0.0f)).xyz);
//Modify End
    return max(0.001f, dot(positionWs - Camera_Position.xyz, cameraForward));
}

float4 PackNRDDiffuseRadianceHitDistance(float3 radiance, float hitDistance, float viewZ, float roughness)
{
    radiance = SanitizeNRDRadiance(radiance);
    if (Camera_NRDDenoiserMode == 1u)
    {
        const float3 hitDistanceParams = Camera_NRDReblurHitDistanceParameters.xyz;
        const float normHitDistance = REBLUR_FrontEnd_GetNormHitDist(
            max(0.0f, hitDistance),
            viewZ,
            hitDistanceParams,
            max(0.001f, roughness));
        return REBLUR_FrontEnd_PackRadianceAndNormHitDist(radiance, normHitDistance, false);
    }

    return RELAX_FrontEnd_PackRadianceAndHitDist(radiance, max(0.0f, hitDistance), false);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    const float depth = DepthTexture.Load(int3(pixel, 0));
//Modify Begin:2026-07-28 by BestHui
    if (depth >= 0.999999f)
    {
        NoisyRadiance[pixel] = 0.0f;
        NRDNoisyRadiance[pixel] = 0.0f;
        HistoryColor[pixel] = 0.0f;
        SceneColor[pixel] = 0.0f;
        return;
    }
//Modify End

    float3 sampleColor = 0.0f;
    float directHitDistance = 0.0f;
    float indirectHitDistance = 0.0f;
    float3 directLighting = 0.0f;
    float3 indirectLightingColor = 0.0f;
    if (Camera_DirectLightingEnabled != 0u)
    {
        const float4 directLightingSample = DirectLightingTexture.Load(int3(pixel, 0));
        directLighting = directLightingSample.rgb;
        directHitDistance = directLightingSample.a;
        sampleColor += directLighting;
    }
    if (Camera_IndirectLightingEnabled != 0u)
    {
        const float4 indirectLighting = IndirectLightingTexture.Load(int3(pixel, 0));
        indirectLightingColor = indirectLighting.rgb;
        indirectHitDistance = indirectLighting.a;
        sampleColor += indirectLightingColor;
    }

    const float directLuminance = dot(max(directLighting, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
    const float indirectLuminance = dot(max(indirectLightingColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
    float directHitDistanceContribution = directLuminance / max(0.0001f, directLuminance + indirectLuminance);
    directHitDistanceContribution = min(directHitDistanceContribution, 0.5f);
    const float hitDistance = lerp(indirectHitDistance, directHitDistance, directHitDistanceContribution);

    const float viewZ = GetGBufferViewZ(pixel);
    const float roughness = GetGBufferRoughness(pixel);
    const float3 nrdDemodulation = GetNRDDiffuseDemodulation(pixel);
    NoisyRadiance[pixel] = float4(SanitizeNRDRadiance(sampleColor), hitDistance);
    NRDNoisyRadiance[pixel] = PackNRDDiffuseRadianceHitDistance(sampleColor / nrdDemodulation, hitDistance, viewZ, roughness);

    if (Camera_AccumulationEnabled == 0u)
    {
//Modify Begin:2026-07-28 by BestHui
        SceneColor[pixel] = float4(SanitizeNRDRadiance(sampleColor), 1.0f);
//Modify End
        return;
    }

    const uint previousSampleCount = Camera_AccumulationFrameIndex;
    float3 accumulatedColor = sampleColor;
    if (previousSampleCount > 0u)
    {
        const float3 history = HistoryColor[pixel].rgb;
        accumulatedColor = (history * float(previousSampleCount) + sampleColor) / float(previousSampleCount + 1u);
    }

    HistoryColor[pixel] = float4(accumulatedColor, float(previousSampleCount + 1u));
//Modify Begin:2026-07-28 by BestHui
    SceneColor[pixel] = float4(SanitizeNRDRadiance(accumulatedColor), 1.0f);
//Modify End
}
