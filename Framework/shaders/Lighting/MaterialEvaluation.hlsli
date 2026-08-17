#ifndef FRAMEWORK_MATERIAL_EVALUATION_HLSLI
#define FRAMEWORK_MATERIAL_EVALUATION_HLSLI

//Modify Begin:2026-07-30 by Hui
#ifndef FRAMEWORK_MATERIAL_SHADING_MODEL
#define FRAMEWORK_MATERIAL_SHADING_MODEL 0
#endif

#define FRAMEWORK_MATERIAL_SHADING_MODEL_PBR 0
#define FRAMEWORK_MATERIAL_SHADING_MODEL_STYLIZED_COMIC 1

static const float FRAMEWORK_MATERIAL_PI = 3.14159265359f;
static const float FRAMEWORK_MATERIAL_INV_PI = 0.31830988618f;

struct FrameworkMaterialSurface
{
    float3 BaseColor;
    float3 SpecularColor;
    float3 NormalWs;
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
};

float FrameworkMaterialFresnelPow5(const float value)
{
    const float valueSquared = value * value;
    return valueSquared * valueSquared * value;
}

float3 FrameworkMaterialFresnelSchlick(const float cosTheta, const float3 f0)
{
    return f0 + (1.0f - f0) * FrameworkMaterialFresnelPow5(saturate(1.0f - cosTheta));
}

float FrameworkMaterialDistributionGgx(
    const float3 normalWs,
    const float3 halfVectorWs,
    const float roughness)
{
    const float alpha = max(0.001f, roughness * roughness);
    const float alphaSquared = alpha * alpha;
    const float normalDotHalf = saturate(dot(normalWs, halfVectorWs));
    const float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    const float denominator = normalDotHalfSquared * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(0.0001f, FRAMEWORK_MATERIAL_PI * denominator * denominator);
}

float FrameworkMaterialGeometrySchlickGgx(const float normalDotView, const float roughness)
{
    const float roughnessBias = roughness + 1.0f;
    const float k = roughnessBias * roughnessBias * 0.125f;
    return normalDotView / max(0.0001f, normalDotView * (1.0f - k) + k);
}

float FrameworkMaterialGeometrySmith(
    const float3 normalWs,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs,
    const float roughness)
{
    return FrameworkMaterialGeometrySchlickGgx(saturate(dot(normalWs, viewDirectionWs)), roughness) *
        FrameworkMaterialGeometrySchlickGgx(saturate(dot(normalWs, lightDirectionWs)), roughness);
}

float3 FrameworkMaterialGetF0(const FrameworkMaterialSurface surface)
{
    return lerp(surface.SpecularColor, surface.BaseColor, saturate(surface.Metallic));
}

float3 FrameworkEvaluatePbrBrdf(
    const FrameworkMaterialSurface surface,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs)
{
    const float3 normalWs = normalize(surface.NormalWs);
    const float3 halfVectorWs = normalize(viewDirectionWs + lightDirectionWs);
    const float roughness = max(0.04f, surface.Roughness);
    const float metallic = saturate(surface.Metallic);
    const float normalDotView = saturate(dot(normalWs, viewDirectionWs));
    const float normalDotLight = saturate(dot(normalWs, lightDirectionWs));
    const float viewDotHalf = saturate(dot(viewDirectionWs, halfVectorWs));
    if (normalDotView <= 0.0f || normalDotLight <= 0.0f || viewDotHalf <= 0.0f)
    {
        return 0.0f;
    }

    const float3 f0 = FrameworkMaterialGetF0(surface);
    const float3 fresnel = FrameworkMaterialFresnelSchlick(viewDotHalf, f0);
    const float distribution = FrameworkMaterialDistributionGgx(normalWs, halfVectorWs, roughness);
    const float geometry = FrameworkMaterialGeometrySmith(normalWs, viewDirectionWs, lightDirectionWs, roughness);
    const float3 specular = distribution * geometry * fresnel /
        max(0.0001f, 4.0f * normalDotView * normalDotLight);
    const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - metallic);
    return diffuseWeight * surface.BaseColor * FRAMEWORK_MATERIAL_INV_PI + specular;
}

float3 FrameworkEvaluateStylizedComicBrdf(
    const FrameworkMaterialSurface surface,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs)
{
    const float3 normalWs = normalize(surface.NormalWs);
    const float normalDotLight = saturate(dot(normalWs, lightDirectionWs));
    const float normalDotView = saturate(dot(normalWs, viewDirectionWs));
    if (normalDotView <= 0.0f || normalDotLight <= 0.0f)
    {
        return 0.0f;
    }

    const float3 halfVectorWs = normalize(viewDirectionWs + lightDirectionWs);
    const float viewDotHalf = saturate(dot(viewDirectionWs, halfVectorWs));
    const float roughness = max(0.04f, surface.Roughness);
    const float metallic = saturate(surface.Metallic);
    const float3 f0 = FrameworkMaterialGetF0(surface);
    const float3 fresnel = FrameworkMaterialFresnelSchlick(viewDotHalf, f0);
    const float distribution = FrameworkMaterialDistributionGgx(normalWs, halfVectorWs, roughness);
    const float geometry = FrameworkMaterialGeometrySmith(normalWs, viewDirectionWs, lightDirectionWs, roughness);
    const float3 physicalSpecular = distribution * geometry * fresnel /
        max(0.0001f, 4.0f * normalDotView * normalDotLight);

    const float diffuseBand = smoothstep(0.18f, 0.24f, normalDotLight);
    const float diffuseRim = smoothstep(0.0f, 0.55f, 1.0f - normalDotView) * 0.12f;
    const float3 shadowTint = lerp(surface.BaseColor * float3(0.35f, 0.43f, 0.60f), surface.BaseColor, diffuseBand);
    const float3 diffuse = shadowTint * (0.36f + 0.64f * diffuseBand + diffuseRim) * (1.0f - metallic);

    const float specularBand = smoothstep(0.12f + 0.72f * roughness, 0.17f + 0.72f * roughness, max(physicalSpecular.r, max(physicalSpecular.g, physicalSpecular.b)));
    const float3 specular = fresnel * specularBand * lerp(0.45f, 1.35f, 1.0f - roughness);
    return diffuse + specular;
}

float3 FrameworkEvaluateMaterialBrdf(
    const FrameworkMaterialSurface surface,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs)
{
#if FRAMEWORK_MATERIAL_SHADING_MODEL == FRAMEWORK_MATERIAL_SHADING_MODEL_STYLIZED_COMIC
    return FrameworkEvaluateStylizedComicBrdf(surface, viewDirectionWs, lightDirectionWs);
#else
    return FrameworkEvaluatePbrBrdf(surface, viewDirectionWs, lightDirectionWs);
#endif
}

float3 FrameworkEvaluateMaterialLighting(
    const FrameworkMaterialSurface surface,
    const float3 viewDirectionWs,
    const float3 lightDirectionWs,
    const float3 radiance)
{
    const float normalDotLight = saturate(dot(normalize(surface.NormalWs), lightDirectionWs));
    if (normalDotLight <= 0.0f)
    {
        return 0.0f;
    }

    return FrameworkEvaluateMaterialBrdf(surface, normalize(viewDirectionWs), lightDirectionWs) *
        radiance * normalDotLight * surface.AmbientOcclusion;
}

//Modify Begin:2026-08-12 by Hui
float FrameworkMaterialLuminance(const float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

float FrameworkMaterialMaxComponent(const float3 value)
{
    return max(value.r, max(value.g, value.b));
}

void FrameworkMaterialBuildOrthonormalBasis(
    const float3 normalWs,
    out float3 tangentWs,
    out float3 bitangentWs)
{
    const float3 up = abs(normalWs.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    tangentWs = normalize(cross(up, normalWs));
    bitangentWs = cross(normalWs, tangentWs);
}

float3 FrameworkMaterialToWorldHemisphere(
    const float3 normalWs,
    const float x,
    const float y,
    const float z)
{
    float3 tangentWs;
    float3 bitangentWs;
    FrameworkMaterialBuildOrthonormalBasis(normalWs, tangentWs, bitangentWs);
    return normalize(tangentWs * x + bitangentWs * y + normalWs * z);
}

float3 FrameworkMaterialSampleCosineHemisphere(
    const float3 normalWs,
    const float2 sample)
{
    const float radius = sqrt(sample.x);
    const float phi = 2.0f * FRAMEWORK_MATERIAL_PI * sample.y;
    return FrameworkMaterialToWorldHemisphere(
        normalWs,
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0f, 1.0f - sample.x)));
}

float3 FrameworkMaterialSampleGgxHalfVector(
    const float3 normalWs,
    const float roughness,
    const float2 sample)
{
    const float alpha = max(0.001f, roughness * roughness);
    const float alphaSquared = alpha * alpha;
    const float phi = 2.0f * FRAMEWORK_MATERIAL_PI * sample.y;
    const float cosTheta = sqrt(
        (1.0f - sample.x) /
        max(0.0001f, 1.0f + (alphaSquared - 1.0f) * sample.x));
    const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    return FrameworkMaterialToWorldHemisphere(
        normalWs,
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta);
}

bool FrameworkSamplePbrDirection(
    const FrameworkMaterialSurface surface,
    const float3 viewDirectionWs,
    const float lobeSelection,
    const float2 directionalSample,
    out float3 directionWs,
    out float3 sampleWeight,
    out float sourcePdf)
{
    directionWs = 0.0f;
    sampleWeight = 0.0f;
    sourcePdf = 0.0f;

    const float3 normalWs = normalize(surface.NormalWs);
    const float3 normalizedViewDirectionWs = normalize(viewDirectionWs);
    const float roughness = max(0.04f, surface.Roughness);
    const float normalDotView = saturate(dot(normalWs, normalizedViewDirectionWs));
    if (normalDotView <= 0.0f)
    {
        return false;
    }

    const float3 f0 = FrameworkMaterialGetF0(surface);
    const float3 fresnel = FrameworkMaterialFresnelSchlick(normalDotView, f0);
    const float diffuseWeight = FrameworkMaterialLuminance(
        surface.BaseColor * (1.0f - saturate(surface.Metallic)));
    const float specularWeight = FrameworkMaterialMaxComponent(fresnel);
    const float specularProbability = clamp(
        specularWeight / max(0.0001f, diffuseWeight + specularWeight),
        0.05f,
        0.95f);

    if (lobeSelection < specularProbability)
    {
        float3 halfVectorWs = FrameworkMaterialSampleGgxHalfVector(
            normalWs,
            roughness,
            directionalSample);
        if (dot(halfVectorWs, normalizedViewDirectionWs) < 0.0f)
        {
            halfVectorWs = -halfVectorWs;
        }
        directionWs = normalize(reflect(-normalizedViewDirectionWs, halfVectorWs));
    }
    else
    {
        directionWs = FrameworkMaterialSampleCosineHemisphere(normalWs, directionalSample);
    }

    const float normalDotLight = saturate(dot(normalWs, directionWs));
    if (normalDotLight <= 0.0f)
    {
        return false;
    }

    const float3 halfVectorWs = normalize(normalizedViewDirectionWs + directionWs);
    const float normalDotHalf = saturate(dot(normalWs, halfVectorWs));
    const float viewDotHalf = saturate(dot(normalizedViewDirectionWs, halfVectorWs));
    const float diffusePdf = normalDotLight * FRAMEWORK_MATERIAL_INV_PI;
    const float specularPdf = FrameworkMaterialDistributionGgx(normalWs, halfVectorWs, roughness) *
        normalDotHalf / max(0.0001f, 4.0f * viewDotHalf);
    sourcePdf = lerp(diffusePdf, specularPdf, specularProbability);
    if (sourcePdf <= 0.00001f)
    {
        return false;
    }

    sampleWeight = FrameworkEvaluateMaterialBrdf(
        surface,
        normalizedViewDirectionWs,
        directionWs) * normalDotLight / sourcePdf;
    sampleWeight *= surface.AmbientOcclusion;
    sampleWeight = min(sampleWeight, 16.0f);
    return FrameworkMaterialMaxComponent(sampleWeight) > 0.0f;
}
//Modify End
//Modify End

#endif
