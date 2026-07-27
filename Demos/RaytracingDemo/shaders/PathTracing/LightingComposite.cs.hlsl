#include "../GBuffer/GBufferLayout.hlsli"
#include "../Scene/SceneCamera.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"

Texture2D<float4> GBufferTextures[GBuffer_Count] : register(t0, space0);
Texture2D<float> DepthTexture : register(t5, space0);
TextureCube Skybox : register(t6, space0);
Texture2D<float4> DirectLightingTexture : register(t7, space0);
Texture2D<float4> IndirectLightingTexture : register(t8, space0);
RWTexture2D<float4> Output : register(u0, space0);
RWTexture2D<float4> Accumulation : register(u1, space0);
RWTexture2D<float4> NoisyRadiance : register(u2, space0);
RWTexture2D<float4> NRDNoisyRadiance : register(u3, space0);
SamplerState LinearWrapSampler : register(s0);

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

float3 SanitizeNRDRadiance(float3 color)
{
    if (!all(isfinite(color)))
    {
        return 0.0f;
    }

    return min(max(color, 0.0f), 250.0f);
}

float3 SampleSkybox(float3 direction)
{
    return Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb * Camera_SkyLight.ColorAndIntensity.rgb * Camera_SkyLight.ColorAndIntensity.w;
}

float3 GetPrimaryRayDirection(uint2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(Camera_Width, Camera_Height);
    float4 clip = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    clip.y = -clip.y;
    float4 view = mul(clip, Camera_InverseProjection);
    view.xyz /= max(view.w, 0.0001f);
    return normalize(mul(float4(normalize(view.xyz), 0.0f), Camera_InverseView).xyz);
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
    const float3 cameraForward = normalize(mul(float4(0.0f, 0.0f, 1.0f, 0.0f), Camera_InverseView).xyz);
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
    if (depth >= 1.0f)
    {
        const float3 skyColor = SampleSkybox(GetPrimaryRayDirection(pixel));
        NoisyRadiance[pixel] = float4(SanitizeNRDRadiance(skyColor), 0.0f);
        NRDNoisyRadiance[pixel] = PackNRDDiffuseRadianceHitDistance(skyColor, 0.0f, 1000000.0f, 1.0f);
        return;
    }

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
        Output[pixel] = float4(ToneMap(sampleColor), 1.0f);
        return;
    }

    const uint previousSampleCount = Camera_AccumulationFrameIndex;
    float3 accumulatedColor = sampleColor;
    if (previousSampleCount > 0u)
    {
        const float3 history = Accumulation[pixel].rgb;
        accumulatedColor = (history * float(previousSampleCount) + sampleColor) / float(previousSampleCount + 1u);
    }

    Accumulation[pixel] = float4(accumulatedColor, float(previousSampleCount + 1u));
    Output[pixel] = float4(ToneMap(accumulatedColor), 1.0f);
}
