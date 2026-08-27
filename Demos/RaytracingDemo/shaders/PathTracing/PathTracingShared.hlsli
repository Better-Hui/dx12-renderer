#ifndef RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_SHARED_HLSLI

#include "../GBuffer/GBufferSampling.hlsli"
#include "../Common/RayOffset.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"
#include "PathTracingRandom.hlsli"
//Modify Begin:2026-07-30 by Hui
#include <Lighting/MaterialEvaluation.hlsli>
//Modify End
//Modify Begin:2026-07-30 by Hui
#ifndef RAYTRACING_DEMO_SOFT_SHADOWS
#define RAYTRACING_DEMO_SOFT_SHADOWS 0
#endif

#ifndef RAYTRACING_DEMO_MAX_BOUNCES
#define RAYTRACING_DEMO_MAX_BOUNCES 3
#endif

#define RAYTRACING_DEMO_SOFT_SHADOW_SAMPLE_COUNT 4u
//Modify End

float3 SampleSkybox(float3 direction)
{
//Modify Begin:2026-08-11 by Hui
    return SampleEnvironmentRadiance(direction);
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

//Modify Begin:2026-07-30 by Hui
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

//Modify Begin:2026-07-30 by Hui
FrameworkMaterialSurface MakeFrameworkMaterialSurface(const SurfaceData surface)
{
    FrameworkMaterialSurface materialSurface;
    materialSurface.BaseColor = surface.Diffuse;
    materialSurface.SpecularColor = surface.Specular;
    materialSurface.NormalWs = surface.NormalWs;
    materialSurface.Metallic = surface.Metallic;
    materialSurface.Roughness = surface.Roughness;
    materialSurface.AmbientOcclusion = surface.AmbientOcclusion;
    return materialSurface;
}

float3 EvaluateMaterialLighting(
    const SurfaceData surface,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs,
    const float3 radiance)
{
    return FrameworkEvaluateMaterialLighting(
        MakeFrameworkMaterialSurface(surface),
        viewDirectionWs,
        lightDirectionWs,
        radiance);
}

float3 EvaluateMaterialLighting(
    const SurfaceData surface,
    const float3 lightDirectionWs,
    const float3 radiance)
{
    return EvaluateMaterialLighting(
        surface,
        Camera_Position.xyz - surface.PositionWs,
        lightDirectionWs,
        radiance);
}
//Modify End

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
    return FrameworkMaterialGetF0(MakeFrameworkMaterialSurface(surface));
}

float3 EvaluateMaterialBrdf(SurfaceData surface, float3 viewDirectionWs, float3 lightDirectionWs)
{
    return FrameworkEvaluateMaterialBrdf(
        MakeFrameworkMaterialSurface(surface),
        normalize(viewDirectionWs),
        normalize(lightDirectionWs));
}

bool SamplePbrDirection(
    const SurfaceData surface,
    const float3 viewDirectionWs,
    const float lobeSelection,
    const float2 directionalSample,
    out float3 directionWs,
    out float3 sampleWeight,
    out float sourcePdf)
{
    return FrameworkSamplePbrDirection(
        MakeFrameworkMaterialSurface(surface),
        viewDirectionWs,
        lobeSelection,
        directionalSample,
        directionWs,
        sampleWeight,
        sourcePdf);
}

//Modify Begin:2026-07-30 by Hui
bool SamplePbrDirection(
    SurfaceData surface,
    float3 viewDirectionWs,
    inout uint rngState,
    out float3 directionWs,
    out float3 sampleWeight,
    out float sourcePdf)
{
    return SamplePbrDirection(
        surface,
        viewDirectionWs,
        Random01(rngState),
        float2(Random01(rngState), Random01(rngState)),
        directionWs,
        sampleWeight,
        sourcePdf);
}
//Modify End

//Modify Begin:2026-07-30 by Hui
float3 EvaluateDirectionalLight(
    DirectionalLightData light,
    SurfaceData surface,
    float3 viewDirectionWs,
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
            lighting += EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, radiance);
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
    return EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, radiance);
#endif
}

float3 EvaluatePointLight(
    PointLightData light,
    SurfaceData surface,
    float3 viewDirectionWs,
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
        lighting += EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, baseRadiance * attenuation);
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
    return EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, radiance);
#endif
}

float EvaluateSpotLightConeAttenuation(const SpotLightData light, const float3 surfaceToLightDirection)
{
    const float coneCosine = dot(-surfaceToLightDirection, normalize(light.DirectionAndCosOuter.xyz));
    const float transition = saturate((coneCosine - light.DirectionAndCosOuter.w) /
        max(1.0e-4f, light.AttenuationAndCosInner.w - light.DirectionAndCosOuter.w));
    return transition * transition * (3.0f - 2.0f * transition);
}

float3 EvaluateSpotLight(
    SpotLightData light,
    SurfaceData surface,
    float3 viewDirectionWs,
    inout uint rngState,
    out float hitDistance)
{
    hitDistance = 0.0f;
    const float3 toLight = light.PositionAndRange.xyz - surface.PositionWs;
    const float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
    {
        return 0.0f;
    }

    const float3 lightDirection = toLight / distanceToLight;
    const float coneAttenuation = EvaluateSpotLightConeAttenuation(light, lightDirection);
    const float nDotL = saturate(dot(surface.NormalWs, lightDirection));
    if (coneAttenuation <= 0.0f || nDotL <= 0.0f)
    {
        return 0.0f;
    }

    const float3 rayOrigin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, lightDirection);
    if (!IsVisibleAlongRay(rayOrigin, lightDirection, distanceToLight))
    {
        return 0.0f;
    }

    hitDistance = distanceToLight;
    const float3 attenuationTerms = light.AttenuationAndCosInner.xyz;
    const float attenuation = rcp(max(
        0.001f,
        attenuationTerms.x + attenuationTerms.y * distanceToLight + attenuationTerms.z * distanceToLight * distanceToLight));
    const float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation * coneAttenuation;
    return EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, radiance);
}
//Modify End

//Modify Begin:2026-08-06 by Hui
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

float3 EvaluateSurfaceEmitter(
    const uint emitterIndex,
    SurfaceData surface,
    float3 viewDirectionWs,
    inout uint rngState,
    out float hitDistance)
{
    hitDistance = 0.0f;
//Modify Begin:2026-08-06 by Hui
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
//Modify Begin:2026-08-06 by Hui
    float3 radiance = lightSample.Emission * lightSample.AreaOverTriangleSelectionPdf * lightFacing / max(0.001f, distanceToLight * distanceToLight);
//Modify End
    return EvaluateMaterialLighting(surface, viewDirectionWs, lightDirection, radiance);
}

//Modify Begin:2026-08-06 by Hui
bool SampleDirectLightIndex(
    const float selectionRandom,
    out uint lightIndex,
    out float inverseSourcePdf)
{
    const uint totalLightCount =
        Camera_DirectionalLightCount +
        Camera_PointLightCount +
        Camera_SpotLightCount +
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

float3 EvaluateDirectLighting(
    SurfaceData surface,
    float3 viewDirectionWs,
    inout uint rngState,
    out float nrdDirectHitDistance)
{
    nrdDirectHitDistance = 0.0f;
//Modify Begin:2026-08-06 by Hui
    uint lightIndex;
    float inverseSourcePdf;
    if (!SampleDirectLightIndex(Random01(rngState), lightIndex, inverseSourcePdf))
    {
        return 0.0f;
    }
//Modify End

    if (lightIndex < Camera_DirectionalLightCount)
    {
        return EvaluateDirectionalLight(
            DirectionalLights[lightIndex],
            surface,
            viewDirectionWs,
            rngState,
            nrdDirectHitDistance) * inverseSourcePdf;
    }

    lightIndex -= Camera_DirectionalLightCount;
    if (lightIndex < Camera_PointLightCount)
    {
        return EvaluatePointLight(
            PointLights[lightIndex],
            surface,
            viewDirectionWs,
            rngState,
            nrdDirectHitDistance) * inverseSourcePdf;
    }

    lightIndex -= Camera_PointLightCount;
    if (lightIndex < Camera_SpotLightCount)
    {
        return EvaluateSpotLight(
            SpotLights[lightIndex],
            surface,
            viewDirectionWs,
            rngState,
            nrdDirectHitDistance) * inverseSourcePdf;
    }

    lightIndex -= Camera_SpotLightCount;
    return EvaluateSurfaceEmitter(
        lightIndex,
        surface,
        viewDirectionWs,
        rngState,
        nrdDirectHitDistance) * inverseSourcePdf;
}

float3 EvaluateDirectLighting(SurfaceData surface, inout uint rngState, out float nrdDirectHitDistance)
{
    return EvaluateDirectLighting(
        surface,
        Camera_Position.xyz - surface.PositionWs,
        rngState,
        nrdDirectHitDistance);
}

//Modify Begin:2026-08-06 by Hui
#if defined(FRAMEWORK_RESTIR_DI_SCENE_ADAPTER) || defined(FRAMEWORK_RESTIR_GI_SCENE_ADAPTER)
struct PathTracingDirectLightSample
{
    float3 DirectionWs;
    float3 Radiance;
    float3 UnshadowedContribution;
    float Distance;
    bool TransportValid;
    bool Valid;
};

uint GetReSTIRDILightCount()
{
    return Camera_DirectionalLightCount + Camera_PointLightCount + Camera_SpotLightCount + Camera_SurfaceEmitterCount;
}

bool SamplePathTracingDirectLightIndex(
    const float selectionRandom,
    out uint lightIndex,
    out float inverseSourcePdf)
{
    return SampleDirectLightIndex(selectionRandom, lightIndex, inverseSourcePdf);
}

PathTracingDirectLightSample SamplePathTracingDirectLight(
    const uint lightIndex,
    const SurfaceData surface,
    const float2 sampleUv)
{
    PathTracingDirectLightSample sample;
    sample.DirectionWs = 0.0f;
    sample.Radiance = 0.0f;
    sample.UnshadowedContribution = 0.0f;
    sample.Distance = 0.0f;
    sample.TransportValid = false;
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
        sample.Radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w;
        sample.UnshadowedContribution = EvaluateMaterialLighting(
            surface,
            sample.DirectionWs,
            sample.Radiance);
        sample.TransportValid = MaxComponent(sample.Radiance) > 0.0f;
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
        sample.Radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation;
        sample.UnshadowedContribution = EvaluateMaterialLighting(
            surface,
            sample.DirectionWs,
            sample.Radiance);
        sample.TransportValid = MaxComponent(sample.Radiance) > 0.0f;
        sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
        return sample;
    }

    index -= Camera_PointLightCount;
    if (index < Camera_SpotLightCount)
    {
        const SpotLightData light = SpotLights[index];
        const float3 toLight = light.PositionAndRange.xyz - surface.PositionWs;
        const float distanceToLight = length(toLight);
        if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
        {
            return sample;
        }

        sample.DirectionWs = toLight / distanceToLight;
        const float coneAttenuation = EvaluateSpotLightConeAttenuation(light, sample.DirectionWs);
        if (coneAttenuation <= 0.0f)
        {
            return sample;
        }

        sample.Distance = distanceToLight;
        const float3 attenuationTerms = light.AttenuationAndCosInner.xyz;
        const float attenuation = rcp(max(
            0.001f,
            attenuationTerms.x + attenuationTerms.y * distanceToLight + attenuationTerms.z * distanceToLight * distanceToLight));
        sample.Radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation * coneAttenuation;
        sample.UnshadowedContribution = EvaluateMaterialLighting(surface, sample.DirectionWs, sample.Radiance);
        sample.TransportValid = MaxComponent(sample.Radiance) > 0.0f;
        sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
        return sample;
    }

    index -= Camera_SpotLightCount;
    if (index >= Camera_SurfaceEmitterCount)
    {
        return sample;
    }

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

    sample.Radiance = areaSample.Emission * areaSample.AreaOverTriangleSelectionPdf * lightFacing /
        max(0.001f, distanceToLight * distanceToLight);
    sample.UnshadowedContribution = EvaluateMaterialLighting(surface, sample.DirectionWs, sample.Radiance);
    sample.TransportValid = MaxComponent(sample.Radiance) > 0.0f;
    sample.Valid = MaxComponent(sample.UnshadowedContribution) > 0.0f;
    return sample;
}

bool IsPathTracingDirectLightSampleVisible(
    const SurfaceData surface,
    const PathTracingDirectLightSample sample)
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

bool IsPathTracingDirectLightTransportVisible(
    const SurfaceData surface,
    const PathTracingDirectLightSample sample)
{
    if (!sample.TransportValid)
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

//Modify Begin:2026-08-11 by Hui
SurfaceData MakeRayHitSurface(const RayPayload payload, const float3 positionWs)
{
    SurfaceData surface;
    surface.Diffuse = saturate(payload.BaseColor);
    surface.Specular = 0.04f;
    surface.PositionWs = positionWs;
    surface.NormalWs = payload.Normal;
    surface.PositionError = payload.PositionError;
    surface.Metallic = payload.Metallic;
    surface.Roughness = payload.Roughness;
    surface.AmbientOcclusion = payload.AmbientOcclusion;
    surface.Valid = true;
    return surface;
}

#if defined(FRAMEWORK_RESTIR_GI_SCENE_ADAPTER)
float3 EvaluateDiffuseReflectance(const SurfaceData surface)
{
    return max(surface.Diffuse, 0.0f) * (1.0f - saturate(surface.Metallic)) * INV_PI;
}

float3 EvaluateDiffuseBounceContribution(
    const SurfaceData surface,
    const float3 directionWs,
    const float3 radiance)
{
    const float cosine = saturate(dot(normalize(surface.NormalWs), directionWs));
    return EvaluateDiffuseReflectance(surface) * cosine * surface.AmbientOcclusion * radiance;
}

bool SampleDiffuseDirection(
    const SurfaceData surface,
    const float2 directionalSample,
    out float3 directionWs,
    out float sourcePdf)
{
    directionWs = 0.0f;
    sourcePdf = 0.0f;
    if (MaxComponent(EvaluateDiffuseReflectance(surface)) <= 0.0f)
    {
        return false;
    }

    const float3 normalWs = normalize(surface.NormalWs);
    directionWs = SampleCosineHemisphere(normalWs, directionalSample);
    const float cosine = saturate(dot(normalWs, directionWs));
    sourcePdf = cosine * INV_PI;
    return sourcePdf > 0.00001f;
}

bool SampleDiffuseDirection(
    const SurfaceData surface,
    inout uint rngState,
    out float3 directionWs,
    out float sourcePdf)
{
    return SampleDiffuseDirection(
        surface,
        float2(Random01(rngState), Random01(rngState)),
        directionWs,
        sourcePdf);
}
float3 EvaluateDiffuseDirectLighting(
    const SurfaceData surface,
    inout uint rngState)
{
    uint lightIndex = 0u;
    float inverseSourcePdf = 0.0f;
    if (!SamplePathTracingDirectLightIndex(Random01(rngState), lightIndex, inverseSourcePdf))
    {
        return 0.0f;
    }

    const PathTracingDirectLightSample sample = SamplePathTracingDirectLight(
        lightIndex,
        surface,
        float2(Random01(rngState), Random01(rngState)));
    if (!sample.TransportValid)
    {
        return 0.0f;
    }

    const float3 contribution = EvaluateDiffuseBounceContribution(
        surface,
        sample.DirectionWs,
        sample.Radiance);
    if (MaxComponent(contribution) <= 0.0f || !IsPathTracingDirectLightTransportVisible(surface, sample))
    {
        return 0.0f;
    }

    return contribution * inverseSourcePdf;
}

float3 TraceDiffuseGatherPathRadiance(
    const SurfaceData initialSurface,
    const float3 initialEmission,
    inout uint rngState)
{
    float3 radiance = initialEmission + EvaluateDiffuseDirectLighting(initialSurface, rngState);
    float3 throughput = 1.0f;
    SurfaceData currentSurface = initialSurface;

#if RAYTRACING_DEMO_MAX_BOUNCES > 2
    [unroll]
    for (uint bounce = 0u; bounce < RAYTRACING_DEMO_MAX_BOUNCES - 2u; ++bounce)
    {
        if (MaxComponent(throughput) < 0.005f)
        {
            break;
        }

        float3 directionWs = 0.0f;
        float sourcePdf = 0.0f;
        if (!SampleDiffuseDirection(currentSurface, rngState, directionWs, sourcePdf))
        {
            break;
        }

        const float cosine = saturate(dot(normalize(currentSurface.NormalWs), directionWs));
        throughput *= EvaluateDiffuseReflectance(currentSurface) * cosine / sourcePdf;
        const float3 rayOrigin = OffsetRayOrigin(
            currentSurface.PositionWs,
            currentSurface.PositionError,
            currentSurface.NormalWs,
            directionWs);
        const RayPayload payload = TraceScene(rayOrigin, directionWs, 10000.0f, RAY_FLAG_NONE);
        if (payload.Hit == 0u)
        {
            radiance += throughput * payload.BaseColor;
            break;
        }

        currentSurface = MakeRayHitSurface(payload, rayOrigin + directionWs * payload.HitT);
        radiance += throughput * (
            payload.Emission +
            EvaluateDiffuseDirectLighting(currentSurface, rngState));
    }
#endif

    return radiance;
}
#endif

float3 TraceGatherPathRadiance(
    const SurfaceData initialSurface,
    const float3 initialEmission,
    const float3 initialViewDirection,
    inout uint rngState)
{
    float3 radiance = initialEmission;
    float3 throughput = 1.0f;
    SurfaceData currentSurface = initialSurface;
    float3 viewDirection = normalize(initialViewDirection);

    float directHitDistance = 0.0f;
    radiance += EvaluateDirectLighting(currentSurface, viewDirection, rngState, directHitDistance);

#if RAYTRACING_DEMO_MAX_BOUNCES > 2
    [unroll]
    for (uint bounce = 0u; bounce < RAYTRACING_DEMO_MAX_BOUNCES - 2u; ++bounce)
    {
        if (MaxComponent(throughput) < 0.005f)
        {
            break;
        }

        float3 direction = 0.0f;
        float3 sampleWeight = 0.0f;
        float sourcePdf = 0.0f;
        if (!SamplePbrDirection(
            currentSurface,
            viewDirection,
            rngState,
            direction,
            sampleWeight,
            sourcePdf))
        {
            break;
        }

        throughput *= sampleWeight;
        const float3 origin = OffsetRayOrigin(
            currentSurface.PositionWs,
            currentSurface.PositionError,
            currentSurface.NormalWs,
            direction);
        const RayPayload payload = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
        if (payload.Hit == 0u)
        {
            radiance += throughput * payload.BaseColor;
            break;
        }

        const float3 positionWs = origin + direction * payload.HitT;
        currentSurface = MakeRayHitSurface(payload, positionWs);
        viewDirection = -direction;
        radiance += throughput * (
            payload.Emission +
            EvaluateDirectLighting(currentSurface, viewDirection, rngState, directHitDistance));
    }
#endif

    return radiance;
}

float3 TraceIndirectLighting(SurfaceData surface, inout uint rngState, out float nrdDiffuseHitDistance)
{
    nrdDiffuseHitDistance = 0.0f;
#if RAYTRACING_DEMO_MAX_BOUNCES <= 1
    return 0.0f;
#else
    const float3 viewDirection = normalize(Camera_Position.xyz - surface.PositionWs);
    float3 direction = 0.0f;
    float3 sampleWeight = 0.0f;
    float sourcePdf = 0.0f;
    if (!SamplePbrDirection(surface, viewDirection, rngState, direction, sampleWeight, sourcePdf))
    {
        return 0.0f;
    }

    const float3 origin = OffsetRayOrigin(surface.PositionWs, surface.PositionError, surface.NormalWs, direction);
    const RayPayload payload = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
    if (payload.Hit == 0u)
    {
        nrdDiffuseHitDistance = 10000.0f;
        return sampleWeight * payload.BaseColor;
    }

    nrdDiffuseHitDistance = max(0.0f, payload.HitT);
    const SurfaceData hitSurface = MakeRayHitSurface(payload, origin + direction * payload.HitT);
    return sampleWeight * TraceGatherPathRadiance(
        hitSurface,
        payload.Emission,
        -direction,
        rngState);
#endif
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
//Modify End
