#ifndef RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI

#include "../GBuffer/GBufferSampling.hlsli"
#include "../Common/RayOffset.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"
#include "PathTracingRandom.hlsli"

//Modify Begin:2026-07-30 by BestHui
#ifndef RAYTRACING_DEMO_SOFT_SHADOWS
#define RAYTRACING_DEMO_SOFT_SHADOWS 0
#endif

#define RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT 4u
//Modify End

float3 SampleSkybox(float3 direction)
{
//Modify Begin:2026-07-30 by BestHui
    return Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb *
        Camera_SkyLight.ColorAndIntensity.rgb *
        Camera_SkyLight.ColorAndIntensity.w;
//Modify End
}

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

float3 GetNRDDiffuseDemodulation(SurfaceData surface)
{
    float metallic = saturate(surface.Metallic);
    float3 diffuseFactor = max(surface.Diffuse * (1.0f - metallic), 0.05f);
    return lerp(diffuseFactor, float3(1.0f, 1.0f, 1.0f), metallic);
}

float4 PackNRDDiffuseRadianceHitDistance(float3 radiance, float hitDistance, float viewZ, float roughness)
{
    radiance = SanitizeNRDRadiance(radiance);
    if (Camera_NRDDenoiserMode == 1u)
    {
        float3 hitDistanceParams = Camera_NRDReblurHitDistanceParameters.xyz;
        float normHitDistance = REBLUR_FrontEnd_GetNormHitDist(
            max(0.0f, hitDistance),
            viewZ,
            hitDistanceParams,
            max(0.001f, roughness));
        return REBLUR_FrontEnd_PackRadianceAndNormHitDist(radiance, normHitDistance, false);
    }

    return RELAX_FrontEnd_PackRadianceAndHitDist(radiance, max(0.0f, hitDistance), false);
}

bool IsVisibleAlongRay(float3 origin, float3 direction, float tMax)
{
    RayPayload shadowPayload = TraceScene(
        origin,
        direction,
        tMax,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH);
    return shadowPayload.Hit == 0u;
}

//Modify Begin:2026-07-30 by BestHui
float3 SampleDirectionalShadowDirection(float3 axis, float angularRadius, inout uint rngState)
{
    float3 tangent;
    float3 bitangent;
    BuildOrthonormalBasis(axis, tangent, bitangent);
    const float clampedRadius = min(max(0.0f, angularRadius), 1.5707963f);
    const float cosTheta = lerp(1.0f, cos(clampedRadius), Random01(rngState));
    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * PI * Random01(rngState);
    return normalize(
        axis * cosTheta +
        tangent * (cos(phi) * sinTheta) +
        bitangent * (sin(phi) * sinTheta));
}

float2 SampleUniformDisk(inout uint rngState)
{
    const float radius = sqrt(Random01(rngState));
    const float phi = 2.0f * PI * Random01(rngState);
    return float2(cos(phi), sin(phi)) * radius;
}
//Modify End

float FresnelPow5(float value)
{
    const float value2 = value * value;
    return value2 * value2 * value;
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * FresnelPow5(saturate(1.0f - cosTheta));
}

float DistributionGGX(float3 normalWs, float3 halfVectorWs, float roughness)
{
    const float a = max(0.001f, roughness * roughness);
    const float a2 = a * a;
    const float nDotH = saturate(dot(normalWs, halfVectorWs));
    const float nDotH2 = nDotH * nDotH;
    const float denom = nDotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(0.0001f, PI * denom * denom);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) * 0.125f;
    return nDotV / max(0.0001f, nDotV * (1.0f - k) + k);
}

float GeometrySmith(float3 normalWs, float3 viewDirectionWs, float3 lightDirectionWs, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(normalWs, viewDirectionWs)), roughness) *
        GeometrySchlickGGX(saturate(dot(normalWs, lightDirectionWs)), roughness);
}

float3 EvaluatePbrLighting(SurfaceData surface, float3 lightDirectionWs, float3 radiance)
{
    const float3 normalWs = normalize(surface.NormalWs);
    const float3 viewDirectionWs = normalize(Camera_Position.xyz - surface.PositionWs);
    const float3 halfVectorWs = normalize(viewDirectionWs + lightDirectionWs);
    const float roughness = max(0.04f, surface.Roughness);
    const float metallic = saturate(surface.Metallic);
    const float nDotL = saturate(dot(normalWs, lightDirectionWs));
    const float nDotV = saturate(dot(normalWs, viewDirectionWs));
    if (nDotL <= 0.0f)
    {
        return 0.0f;
    }

    const float3 f0 = lerp(surface.Specular, surface.Diffuse, metallic);
    const float3 f = FresnelSchlick(saturate(dot(halfVectorWs, viewDirectionWs)), f0);
    const float d = DistributionGGX(normalWs, halfVectorWs, roughness);
    const float g = GeometrySmith(normalWs, viewDirectionWs, lightDirectionWs, roughness);
    const float3 specular = (d * g * f) / max(0.0001f, 4.0f * nDotV * nDotL);
    const float3 kd = (1.0f - f) * (1.0f - metallic);
    return (kd * surface.Diffuse * INV_PI + specular) * radiance * nDotL * surface.AmbientOcclusion;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float MaxComponent(float3 value)
{
    return max(value.r, max(value.g, value.b));
}

float3 GetSurfaceF0(SurfaceData surface)
{
    return lerp(surface.Specular, surface.Diffuse, saturate(surface.Metallic));
}

float3 EvaluatePbrBrdf(SurfaceData surface, float3 viewDirectionWs, float3 lightDirectionWs)
{
    float3 normalWs = normalize(surface.NormalWs);
    float3 halfVectorWs = normalize(viewDirectionWs + lightDirectionWs);
    float roughness = max(0.04f, surface.Roughness);
    float metallic = saturate(surface.Metallic);
    float nDotV = saturate(dot(normalWs, viewDirectionWs));
    float nDotL = saturate(dot(normalWs, lightDirectionWs));
    float vDotH = saturate(dot(viewDirectionWs, halfVectorWs));
    if (nDotV <= 0.0f || nDotL <= 0.0f || vDotH <= 0.0f)
    {
        return 0.0f;
    }

    float3 f0 = GetSurfaceF0(surface);
    float3 f = FresnelSchlick(vDotH, f0);
    float d = DistributionGGX(normalWs, halfVectorWs, roughness);
    float g = GeometrySmith(normalWs, viewDirectionWs, lightDirectionWs, roughness);
    float3 specular = (d * g * f) / max(0.0001f, 4.0f * nDotV * nDotL);
    float3 kd = (1.0f - f) * (1.0f - metallic);
    return kd * surface.Diffuse * INV_PI + specular;
}

bool SamplePbrDirection(SurfaceData surface, float3 viewDirectionWs, inout uint rngState, out float3 directionWs, out float3 sampleWeight)
{
    directionWs = 0.0f;
    sampleWeight = 0.0f;

    float3 normalWs = normalize(surface.NormalWs);
    viewDirectionWs = normalize(viewDirectionWs);
    float roughness = max(0.04f, surface.Roughness);
    float nDotV = saturate(dot(normalWs, viewDirectionWs));
    if (nDotV <= 0.0f)
    {
        return false;
    }

    float3 f0 = GetSurfaceF0(surface);
    float3 fresnel = FresnelSchlick(nDotV, f0);
    float diffuseWeight = Luminance(surface.Diffuse * (1.0f - saturate(surface.Metallic)));
    float specularWeight = MaxComponent(fresnel);
    float specularProbability = specularWeight / max(0.0001f, diffuseWeight + specularWeight);
    specularProbability = clamp(specularProbability, 0.05f, 0.95f);

    bool sampleSpecular = Random01(rngState) < specularProbability;
    if (sampleSpecular)
    {
        float3 halfVectorWs = SampleGGXHalfVector(normalWs, roughness, rngState);
        if (dot(halfVectorWs, viewDirectionWs) < 0.0f)
        {
            halfVectorWs = -halfVectorWs;
        }
        directionWs = normalize(reflect(-viewDirectionWs, halfVectorWs));
    }
    else
    {
        directionWs = SampleCosineHemisphere(normalWs, rngState);
    }

    float nDotL = saturate(dot(normalWs, directionWs));
    if (nDotL <= 0.0f)
    {
        return false;
    }

    float3 halfVector = normalize(viewDirectionWs + directionWs);
    float nDotH = saturate(dot(normalWs, halfVector));
    float vDotH = saturate(dot(viewDirectionWs, halfVector));
    float diffusePdf = nDotL * INV_PI;
    float specularPdf = DistributionGGX(normalWs, halfVector, roughness) * nDotH / max(0.0001f, 4.0f * vDotH);
    float pdf = lerp(diffusePdf, specularPdf, specularProbability);
    if (pdf <= 0.00001f)
    {
        return false;
    }

    float3 brdf = EvaluatePbrBrdf(surface, viewDirectionWs, directionWs);
    sampleWeight = brdf * nDotL / pdf;
    sampleWeight *= surface.AmbientOcclusion;
    sampleWeight = min(sampleWeight, 16.0f);
    return MaxComponent(sampleWeight) > 0.0f;
}

//Modify Begin:2026-07-30 by BestHui
float3 EvaluateDirectionalLight(
    DirectionalLightData light,
    SurfaceData surface,
    inout uint rngState,
    out float hitDistance)
{
    hitDistance = 0.0f;
#if RAYTRACING_DEMO_SOFT_SHADOWS
    const float3 lightAxis = normalize(light.DirectionAndAngularRadius.xyz);
    const float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w;
    float3 lighting = 0.0f;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT; ++sampleIndex)
    {
        const float3 lightDirection = SampleDirectionalShadowDirection(
            lightAxis,
            light.DirectionAndAngularRadius.w,
            rngState);
        const float nDotL = saturate(dot(surface.NormalWs, lightDirection));
        if (nDotL <= 0.0f)
        {
            continue;
        }

        const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
        if (IsVisibleAlongRay(rayOrigin, lightDirection, 10000.0f))
        {
            lighting += EvaluatePbrLighting(surface, lightDirection, radiance);
        }
    }

    hitDistance = 10000.0f;
    return lighting / float(RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT);
#else
    const float3 lightDirection = normalize(light.DirectionAndAngularRadius.xyz);
    const float nDotL = saturate(dot(surface.NormalWs, lightDirection));
    if (nDotL <= 0.0f)
    {
        return 0.0f;
    }

    const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
    if (!IsVisibleAlongRay(rayOrigin, lightDirection, 10000.0f))
    {
        return 0.0f;
    }

    hitDistance = 10000.0f;
    const float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w;
    return EvaluatePbrLighting(surface, lightDirection, radiance);
#endif
}

float3 EvaluatePointLight(
    PointLightData light,
    SurfaceData surface,
    inout uint rngState,
    out float hitDistance)
{
    hitDistance = 0.0f;
#if RAYTRACING_DEMO_SOFT_SHADOWS
    const float3 toLight = light.PositionAndRange.xyz - surface.PositionWs;
    const float baseDistanceToLight = length(toLight);
    if (baseDistanceToLight <= 0.001f || baseDistanceToLight > light.PositionAndRange.w)
    {
        return 0.0f;
    }

    const float3 baseLightDirection = toLight / baseDistanceToLight;
    float3 tangent;
    float3 bitangent;
    BuildOrthonormalBasis(baseLightDirection, tangent, bitangent);
    const float3 baseRadiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w;
    float3 lighting = 0.0f;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT; ++sampleIndex)
    {
        const float2 diskSample = SampleUniformDisk(rngState) * light.Attenuation.w;
        const float3 samplePosition = light.PositionAndRange.xyz + tangent * diskSample.x + bitangent * diskSample.y;
        const float3 sampleToLight = samplePosition - surface.PositionWs;
        const float distanceToLight = length(sampleToLight);
        if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
        {
            continue;
        }

        const float3 lightDirection = sampleToLight / distanceToLight;
        const float nDotL = saturate(dot(surface.NormalWs, lightDirection));
        if (nDotL <= 0.0f)
        {
            continue;
        }

        const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
        if (!IsVisibleAlongRay(rayOrigin, lightDirection, distanceToLight))
        {
            continue;
        }

        const float3 attenuationTerms = light.Attenuation.xyz;
        const float attenuation = rcp(max(
            0.001f,
            attenuationTerms.x +
            attenuationTerms.y * distanceToLight +
            attenuationTerms.z * distanceToLight * distanceToLight));
        lighting += EvaluatePbrLighting(surface, lightDirection, baseRadiance * attenuation);
    }

    hitDistance = baseDistanceToLight;
    return lighting / float(RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT);
#else
    float3 toLight = light.PositionAndRange.xyz - surface.PositionWs;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
    {
        return 0.0f;
    }

    float3 lightDirection = toLight / distanceToLight;
    float nDotL = saturate(dot(surface.NormalWs, lightDirection));
    if (nDotL <= 0.0f)
    {
        return 0.0f;
    }

    const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
    if (!IsVisibleAlongRay(rayOrigin, lightDirection, distanceToLight))
    {
        return 0.0f;
    }

    hitDistance = distanceToLight;
    float3 attenuationTerms = light.Attenuation.xyz;
    float attenuation = rcp(max(
        0.001f,
        attenuationTerms.x + attenuationTerms.y * distanceToLight + attenuationTerms.z * distanceToLight * distanceToLight));
    float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation;
    return EvaluatePbrLighting(surface, lightDirection, radiance);
#endif
}
//Modify End

float3 EvaluateAreaLight(AreaLightData light, SurfaceData surface, inout uint rngState, out float hitDistance)
{
    hitDistance = 0.0f;
    float2 sampleUv = float2(Random01(rngState), Random01(rngState)) * 2.0f - 1.0f;
    float3 samplePosition =
        light.PositionAndRange.xyz +
        light.AxisUAndExtent.xyz * light.AxisUAndExtent.w * sampleUv.x +
        light.AxisVAndExtent.xyz * light.AxisVAndExtent.w * sampleUv.y;

    float3 toLight = samplePosition - surface.PositionWs;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
    {
        return 0.0f;
    }

    float3 lightDirection = toLight / distanceToLight;
    float nDotL = saturate(dot(surface.NormalWs, lightDirection));
    float lightFacing = saturate(dot(normalize(light.NormalAndType.xyz), -lightDirection));
    if (nDotL <= 0.0f || lightFacing <= 0.0f)
    {
        return 0.0f;
    }

    const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
    if (!IsVisibleAlongRay(rayOrigin, lightDirection, distanceToLight))
    {
        return 0.0f;
    }

    hitDistance = distanceToLight;
    float area = max(0.0001f, 4.0f * light.AxisUAndExtent.w * light.AxisVAndExtent.w);
    float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * area * lightFacing / max(0.001f, distanceToLight * distanceToLight);
    return EvaluatePbrLighting(surface, lightDirection, radiance);
}

float3 EvaluateDirectLighting(SurfaceData surface, inout uint rngState, out float nrdDirectHitDistance)
{
    nrdDirectHitDistance = 0.0f;
    uint directionalLightCount = Camera_DirectionalLightCount;
    uint pointLightCount = Camera_PointLightCount;
    uint areaLightCount = Camera_AreaLightCount;
    uint totalLightCount = directionalLightCount + pointLightCount + areaLightCount;
    if (totalLightCount == 0u)
    {
        return 0.0f;
    }

    uint lightIndex = min(uint(Random01(rngState) * float(totalLightCount)), totalLightCount - 1u);
    if (lightIndex < directionalLightCount)
    {
        return EvaluateDirectionalLight(DirectionalLights[lightIndex], surface, rngState, nrdDirectHitDistance) * float(totalLightCount);
    }

    lightIndex -= directionalLightCount;
    if (lightIndex < pointLightCount)
    {
        return EvaluatePointLight(PointLights[lightIndex], surface, rngState, nrdDirectHitDistance) * float(totalLightCount);
    }

    lightIndex -= pointLightCount;
    return EvaluateAreaLight(AreaLights[lightIndex], surface, rngState, nrdDirectHitDistance) * float(totalLightCount);
}

//Modify Begin:2026-08-05 by BestHui
#if defined(FRAMEWORK_RESTIR_DI_SCENE_ADAPTER)
struct ReSTIRDIDirectLightSample
{
    float3 DirectionWs;
    float3 UnshadowedContribution;
    float Distance;
    bool Valid;
};

uint GetReSTIRDILightCount()
{
    return Camera_DirectionalLightCount + Camera_PointLightCount + Camera_AreaLightCount;
}

ReSTIRDIDirectLightSample SampleReSTIRDIDirectLight(
    const uint lightIndex,
    const SurfaceData surface,
    const float2 sampleUv)
{
    ReSTIRDIDirectLightSample sample;
    sample.DirectionWs = 0.0f;
    sample.UnshadowedContribution = 0.0f;
    sample.Distance = 0.0f;
    sample.Valid = false;

    uint index = lightIndex;
    if (index < Camera_DirectionalLightCount)
    {
        const DirectionalLightData light = DirectionalLights[index];
#if RAYTRACING_DEMO_SOFT_SHADOWS
        uint sampleRandomState = asuint(sampleUv.x * 65535.0f) ^ (asuint(sampleUv.y * 65535.0f) << 16u);
        sample.DirectionWs = SampleDirectionalShadowDirection(
            normalize(light.DirectionAndAngularRadius.xyz),
            light.DirectionAndAngularRadius.w,
            sampleRandomState);
#else
        sample.DirectionWs = normalize(light.DirectionAndAngularRadius.xyz);
#endif
        sample.Distance = 10000.0f;
        sample.UnshadowedContribution = EvaluatePbrLighting(
            surface,
            sample.DirectionWs,
            light.ColorAndIntensity.rgb * light.ColorAndIntensity.w);
        sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
        return sample;
    }

    index -= Camera_DirectionalLightCount;
    if (index < Camera_PointLightCount)
    {
        const PointLightData light = PointLights[index];
#if RAYTRACING_DEMO_SOFT_SHADOWS
        const float3 toLightCenter = light.PositionAndRange.xyz - surface.PositionWs;
        const float centerDistance = length(toLightCenter);
        if (centerDistance <= 0.001f || centerDistance > light.PositionAndRange.w)
        {
            return sample;
        }

        float3 tangent;
        float3 bitangent;
        BuildOrthonormalBasis(toLightCenter / centerDistance, tangent, bitangent);
        const float radius = sqrt(saturate(sampleUv.x)) * light.Attenuation.w;
        const float angle = sampleUv.y * 6.28318530718f;
        const float2 diskSample = radius * float2(cos(angle), sin(angle));
        const float3 samplePosition = light.PositionAndRange.xyz + tangent * diskSample.x + bitangent * diskSample.y;
        const float3 toLight = samplePosition - surface.PositionWs;
#else
        const float3 toLight = light.PositionAndRange.xyz - surface.PositionWs;
#endif
        const float distanceToLight = length(toLight);
        if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
        {
            return sample;
        }

        sample.DirectionWs = toLight / distanceToLight;
        sample.Distance = distanceToLight;
        const float3 attenuationTerms = light.Attenuation.xyz;
        const float attenuation = rcp(max(
            0.001f,
            attenuationTerms.x + attenuationTerms.y * distanceToLight + attenuationTerms.z * distanceToLight * distanceToLight));
        sample.UnshadowedContribution = EvaluatePbrLighting(
            surface,
            sample.DirectionWs,
            light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation);
        sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
        return sample;
    }

    index -= Camera_PointLightCount;
    if (index >= Camera_AreaLightCount)
    {
        return sample;
    }

    const AreaLightData light = AreaLights[index];
    const float2 areaUv = sampleUv * 2.0f - 1.0f;
    const float3 samplePosition = light.PositionAndRange.xyz +
        light.AxisUAndExtent.xyz * (areaUv.x * light.AxisUAndExtent.w) +
        light.AxisVAndExtent.xyz * (areaUv.y * light.AxisVAndExtent.w);
    const float3 toLight = samplePosition - surface.PositionWs;
    const float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
    {
        return sample;
    }

    sample.DirectionWs = toLight / distanceToLight;
    sample.Distance = distanceToLight;
    const float lightFacing = saturate(dot(-sample.DirectionWs, normalize(light.NormalAndType.xyz)));
    if (lightFacing <= 0.0f)
    {
        return sample;
    }

    const float area = max(0.0001f, 4.0f * light.AxisUAndExtent.w * light.AxisVAndExtent.w);
    const float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * area * lightFacing /
        max(0.001f, distanceToLight * distanceToLight);
    sample.UnshadowedContribution = EvaluatePbrLighting(surface, sample.DirectionWs, radiance);
    sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
    return sample;
}

bool IsReSTIRDIDirectLightSampleVisible(
    const SurfaceData surface,
    const ReSTIRDIDirectLightSample sample)
{
    if (!sample.Valid)
    {
        return false;
    }

    const float3 rayOrigin = OffsetRayOrigin(
        surface.PositionWs,
        surface.PositionError,
        surface.NormalWs,
        sample.DirectionWs);
    return IsVisibleAlongRay(rayOrigin, sample.DirectionWs, sample.Distance);
}

#endif
//Modify End

float3 TraceIndirectLighting(SurfaceData surface, inout uint rngState, out float nrdDiffuseHitDistance)
{
    nrdDiffuseHitDistance = 0.0f;
    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    float3 viewDirection = normalize(Camera_Position.xyz - surface.PositionWs);
    float3 direction = 0.0f;
    float3 sampleWeight = 0.0f;
    if (!SamplePbrDirection(surface, viewDirection, rngState, direction, sampleWeight))
    {
        return radiance;
    }
    throughput *= sampleWeight;
    float3 origin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, direction);
    uint bounceCount = min(Camera_MaxBounces, 5u);

    [loop]
    for (uint bounce = 0u; bounce < bounceCount; ++bounce)
    {
        RayPayload payload = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
        if (payload.Hit == 0u)
        {
            if (bounce == 0u)
            {
                nrdDiffuseHitDistance = 10000.0f;
            }
            radiance += throughput * payload.BaseColor;
            break;
        }

        if (bounce == 0u)
        {
            nrdDiffuseHitDistance = max(0.0f, payload.HitT);
        }

        float3 positionWs = origin + direction * payload.HitT;
        SurfaceData hitSurface;
        hitSurface.Diffuse = saturate(payload.BaseColor);
        hitSurface.Specular = 0.04f;
        hitSurface.PositionWs = positionWs;
        hitSurface.NormalWs = payload.Normal;
        hitSurface.PositionError = payload.PositionError;
        hitSurface.Metallic = payload.Metallic;
        hitSurface.Roughness = payload.Roughness;
        hitSurface.AmbientOcclusion = payload.AmbientOcclusion;
        hitSurface.Valid = true;

        float directHitDistance = 0.0f;
        radiance += throughput * EvaluateDirectLighting(hitSurface, rngState, directHitDistance);

        if (max(throughput.r, max(throughput.g, throughput.b)) < 0.005f)
        {
            break;
        }

        viewDirection = -direction;
        if (!SamplePbrDirection(hitSurface, viewDirection, rngState, direction, sampleWeight))
        {
            break;
        }
        throughput *= sampleWeight;
        origin = OffsetRayOrigin(positionWs, payload.PositionError, hitSurface.NormalWs, direction);
    }

    return radiance;
}

void WriteDirectLightingOutput(uint2 pixel, uint width, uint frameIndex)
{
    SurfaceData surface = LoadGBufferSurface(pixel);
    if (!surface.Valid)
    {
        DirectLighting[pixel] = 0.0f;
        return;
    }

    uint rngState = InitializeRandomState(pixel, width, frameIndex, 0x1234abcdu);
    float nrdDirectHitDistance = 0.0f;
    DirectLighting[pixel] = float4(EvaluateDirectLighting(surface, rngState, nrdDirectHitDistance), nrdDirectHitDistance);
}

void WriteIndirectLightingOutput(uint2 pixel, uint width, uint frameIndex)
{
    SurfaceData surface = LoadGBufferSurface(pixel);
    if (!surface.Valid)
    {
        IndirectLighting[pixel] = 0.0f;
        return;
    }

    uint rngState = InitializeRandomState(pixel, width, frameIndex, 0x9E3779B9u);
    float nrdDiffuseHitDistance = 0.0f;
    IndirectLighting[pixel] = float4(TraceIndirectLighting(surface, rngState, nrdDiffuseHitDistance), nrdDiffuseHitDistance);
}

#endif
