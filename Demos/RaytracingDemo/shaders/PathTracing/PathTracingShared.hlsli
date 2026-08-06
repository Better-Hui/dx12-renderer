#ifndef RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI

#include "../GBuffer/GBufferSampling.hlsli"
#include "../Common/RayOffset.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"
#include "PathTracingRandom.hlsli"
//Modify Begin:2026-08-06 by BestHui
#include <Common/EnvironmentTexture.hlsli>
//Modify End

//Modify Begin:2026-07-30 by BestHui
#ifndef RAYTRACING_DEMO_SOFT_SHADOWS
#define RAYTRACING_DEMO_SOFT_SHADOWS 0
#endif

#define RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT 4u
//Modify End

float3 SampleSkybox(float3 direction)
{
//Modify Begin:2026-08-06 by BestHui
#if RAYTRACING_DEMO_ENVIRONMENT_PROJECTION == 1
    const float2 uv = float2(
        atan2(direction.z, direction.x) / (2.0f * PI) + 0.5f,
        acos(clamp(direction.y, -1.0f, 1.0f)) / PI);
    return Skybox.SampleLevel(LinearWrapSampler, uv, 0.0f).rgb *
#elif RAYTRACING_DEMO_ENVIRONMENT_PROJECTION == 2
    return Skybox.SampleLevel(
        LinearWrapSampler,
        FrameworkDirectionToHorizontalCubemapStripUv(direction),
        0.0f).rgb *
#else
    return Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb *
#endif
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

//Modify Begin:2026-08-06 by BestHui
struct SurfaceEmitterSample
{
    float3 PositionWs;
    float3 NormalWs;
    float3 Emission;
    float AreaOverTriangleSelectionPdf;
    float MaxDistance;
    bool Valid;
};

float3 TransformSurfaceEmitterVector(const SurfaceEmitterInstanceData instance, const float3 localVector)
{
    return instance.AxisX.xyz * localVector.x +
        instance.AxisY.xyz * localVector.y +
        instance.AxisZ.xyz * localVector.z;
}

float SurfaceEmitterHash01(const float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

SurfaceEmitterSample SampleSurfaceEmitter(const uint emitterIndex, const float2 sampleUv)
{
    SurfaceEmitterSample sample;
    sample.PositionWs = 0.0f;
    sample.NormalWs = 0.0f;
    sample.Emission = 0.0f;
    sample.AreaOverTriangleSelectionPdf = 0.0f;
    sample.MaxDistance = 0.0f;
    sample.Valid = false;

    if (emitterIndex >= Camera_SurfaceEmitterCount)
    {
        return sample;
    }

    const SurfaceEmitterInstanceData instance = SurfaceEmitterInstances[emitterIndex];
    const SurfaceEmitterGeometryData geometry = SurfaceEmitterGeometries[instance.GeometryIndex];
    if (geometry.TriangleCount == 0u)
    {
        return sample;
    }

    const float target = min(saturate(sampleUv.x), 0.99999994f);
    uint low = 0u;
    uint high = geometry.TriangleCount - 1u;
    [loop]
    while (low < high)
    {
        const uint middle = low + (high - low) / 2u;
        if (target <= SurfaceEmitterTriangleCdf[geometry.TriangleCdfOffset + middle])
        {
            high = middle;
        }
        else
        {
            low = middle + 1u;
        }
    }

    const uint triangleIndex = geometry.TriangleOffset + low;
    const float previousCdf = low == 0u ? 0.0f : SurfaceEmitterTriangleCdf[geometry.TriangleCdfOffset + low - 1u];
    const float triangleSelectionPdf = SurfaceEmitterTriangleCdf[geometry.TriangleCdfOffset + low] - previousCdf;
    if (triangleSelectionPdf <= 0.0f)
    {
        return sample;
    }

    const SurfaceEmitterTriangleData surfaceTriangle = SurfaceEmitterTriangles[triangleIndex];
    const float sqrtU = sqrt(saturate(sampleUv.y));
    const float sampleV = SurfaceEmitterHash01(sampleUv);
    const float3 barycentrics = float3(
        1.0f - sqrtU,
        sqrtU * sampleV,
        sqrtU * (1.0f - sampleV));
    const float3 localPosition0 = surfaceTriangle.Position0.xyz;
    const float3 localPosition1 = surfaceTriangle.Position1.xyz;
    const float3 localPosition2 = surfaceTriangle.Position2.xyz;
    const float3 worldPosition0 = instance.OriginAndRange.xyz + TransformSurfaceEmitterVector(instance, localPosition0);
    const float3 worldPosition1 = instance.OriginAndRange.xyz + TransformSurfaceEmitterVector(instance, localPosition1);
    const float3 worldPosition2 = instance.OriginAndRange.xyz + TransformSurfaceEmitterVector(instance, localPosition2);
    const float3 triangleCross = cross(worldPosition1 - worldPosition0, worldPosition2 - worldPosition0);
    const float doubleArea = length(triangleCross);
    if (doubleArea <= 1.0e-8f)
    {
        return sample;
    }

    float3 emission = instance.EmissionAndIntensity.rgb * instance.EmissionAndIntensity.w;
    if ((instance.Flags & SurfaceEmitterInstanceFlagUseMaterialEmission) != 0u &&
        instance.MaterialIndex != SurfaceEmitterInvalidMaterialIndex)
    {
        const MaterialData material = Materials[instance.MaterialIndex];
        const float2 uv = surfaceTriangle.Uv0Uv1.xy * barycentrics.x +
            surfaceTriangle.Uv0Uv1.zw * barycentrics.y +
            surfaceTriangle.Uv2AndPadding.xy * barycentrics.z;
        const float3 emissionMap = material.HasEmissionMap != 0u
            ? SampleBindlessTexture2DLevel(material.EmissionTextureIndex, LinearWrapSampler, uv, 0.0f).rgb
            : 1.0f;
        emission *= emissionMap;
    }

    sample.PositionWs = worldPosition0 * barycentrics.x + worldPosition1 * barycentrics.y + worldPosition2 * barycentrics.z;
    sample.NormalWs = triangleCross / doubleArea;
    sample.Emission = emission;
    sample.AreaOverTriangleSelectionPdf = 0.5f * doubleArea / triangleSelectionPdf;
    sample.MaxDistance = instance.OriginAndRange.w;
    sample.Valid = MaxComponent(sample.Emission) > 0.0f;
    return sample;
}
//Modify End

float3 EvaluateSurfaceEmitter(const uint emitterIndex, SurfaceData surface, inout uint rngState, out float hitDistance)
{
    hitDistance = 0.0f;
//Modify Begin:2026-08-06 by BestHui
    const SurfaceEmitterSample lightSample = SampleSurfaceEmitter(emitterIndex, float2(Random01(rngState), Random01(rngState)));
    if (!lightSample.Valid)
    {
        return 0.0f;
    }
//Modify End

    float3 toLight = lightSample.PositionWs - surface.PositionWs;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > lightSample.MaxDistance)
    {
        return 0.0f;
    }

    float3 lightDirection = toLight / distanceToLight;
    float nDotL = saturate(dot(surface.NormalWs, lightDirection));
    float lightFacing = saturate(dot(lightSample.NormalWs, -lightDirection));
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
//Modify Begin:2026-08-06 by BestHui
    float3 radiance = lightSample.Emission * lightSample.AreaOverTriangleSelectionPdf * lightFacing / max(0.001f, distanceToLight * distanceToLight);
//Modify End
    return EvaluatePbrLighting(surface, lightDirection, radiance);
}

//Modify Begin:2026-08-06 by BestHui
bool SampleDirectLightIndex(
    const float selectionRandom,
    out uint lightIndex,
    out float inverseSourcePdf)
{
    const uint totalLightCount =
        Camera_DirectionalLightCount +
        Camera_PointLightCount +
        Camera_SurfaceEmitterCount;
    if (totalLightCount == 0u || DirectLightCdf[totalLightCount - 1u] <= 0.0f)
    {
        lightIndex = 0u;
        inverseSourcePdf = 0.0f;
        return false;
    }

    const float target = max(1.0e-7f, min(saturate(selectionRandom), 0.99999994f));
    uint low = 0u;
    uint high = totalLightCount - 1u;
    [loop]
    while (low < high)
    {
        const uint middle = low + (high - low) / 2u;
        if (target <= DirectLightCdf[middle])
        {
            high = middle;
        }
        else
        {
            low = middle + 1u;
        }
    }

    lightIndex = low;
    const float previousCdf = lightIndex == 0u ? 0.0f : DirectLightCdf[lightIndex - 1u];
    inverseSourcePdf = rcp(max(1.0e-6f, DirectLightCdf[lightIndex] - previousCdf));
    return true;
}
//Modify End

float3 EvaluateDirectLighting(SurfaceData surface, inout uint rngState, out float nrdDirectHitDistance)
{
    nrdDirectHitDistance = 0.0f;
//Modify Begin:2026-08-06 by BestHui
    uint lightIndex;
    float inverseSourcePdf;
    if (!SampleDirectLightIndex(Random01(rngState), lightIndex, inverseSourcePdf))
    {
        return 0.0f;
    }
//Modify End

    if (lightIndex < Camera_DirectionalLightCount)
    {
        return EvaluateDirectionalLight(DirectionalLights[lightIndex], surface, rngState, nrdDirectHitDistance) * inverseSourcePdf;
    }

    lightIndex -= Camera_DirectionalLightCount;
    if (lightIndex < Camera_PointLightCount)
    {
        return EvaluatePointLight(PointLights[lightIndex], surface, rngState, nrdDirectHitDistance) * inverseSourcePdf;
    }

    lightIndex -= Camera_PointLightCount;
    return EvaluateSurfaceEmitter(lightIndex, surface, rngState, nrdDirectHitDistance) * inverseSourcePdf;
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
    return Camera_DirectionalLightCount + Camera_PointLightCount + Camera_SurfaceEmitterCount;
}

//Modify Begin:2026-08-06 by BestHui
bool SampleReSTIRDIDirectLightIndex(
    const float selectionRandom,
    out uint lightIndex,
    out float inverseSourcePdf)
{
    return SampleDirectLightIndex(selectionRandom, lightIndex, inverseSourcePdf);
}
//Modify End

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
    if (index >= Camera_SurfaceEmitterCount)
    {
        return sample;
    }

//Modify Begin:2026-08-06 by BestHui
    const SurfaceEmitterSample areaSample = SampleSurfaceEmitter(index, sampleUv);
    if (!areaSample.Valid)
    {
        return sample;
    }

    const float3 toLight = areaSample.PositionWs - surface.PositionWs;
    const float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > areaSample.MaxDistance)
    {
        return sample;
    }

    sample.DirectionWs = toLight / distanceToLight;
    sample.Distance = distanceToLight;
    const float lightFacing = saturate(dot(-sample.DirectionWs, areaSample.NormalWs));
    if (lightFacing <= 0.0f)
    {
        return sample;
    }

    const float3 radiance = areaSample.Emission * areaSample.AreaOverTriangleSelectionPdf * lightFacing /
        max(0.001f, distanceToLight * distanceToLight);
//Modify End
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

//Modify Begin:2026-08-06 by BestHui
        radiance += throughput * payload.Emission;
//Modify End
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
