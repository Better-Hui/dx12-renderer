#ifndef RAYTRACING_DEMO_PATH_TRACING_GEOMETRY_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_GEOMETRY_HLSLI

#include "../Common/RayOffset.hlsli"
#include "../Scene/SceneResources.hlsli"
#include "PathTracingPayload.hlsli"

uint LoadIndex(uint indexBufferIndex, uint indexNumber)
{
//Modify Begin:2026-07-30 by BestHui
    return LoadBindlessIndex(indexBufferIndex, indexNumber);
//Modify End
}

RayPayload MakeMissPayload(float3 rayDirection)
{
    RayPayload payload;
    payload.Hit = 0u;
    payload.HitT = 0.0f;
//Modify Begin:2026-07-30 by BestHui
    payload.BaseColor = Camera_SkyLight.ColorAndIntensity.rgb * Camera_SkyLight.ColorAndIntensity.w;
//Modify End
    payload.Normal = 0.0f;
    payload.PositionError = 0.0f;
    payload.Metallic = 0.0f;
    payload.Roughness = 1.0f;
    payload.AmbientOcclusion = 1.0f;
    payload.Padding0 = 0u;
    return payload;
}

float3 UnpackNormalMap(float3 normal)
{
    return normal * 2.0f - 1.0f;
}

RayPayload MakeTrianglePayload(
    GeometryData geometry,
    MaterialData material,
    uint primitiveIndex,
    float2 hitBarycentrics,
    float3 worldRayDirection,
    float3x4 objectToWorld,
    float hitT)
{
    const uint firstIndex = primitiveIndex * 3u;
    const uint i0 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 0u);
    const uint i1 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 1u);
    const uint i2 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 2u);

//Modify Begin:2026-07-30 by BestHui
    const VertexAttributes v0 = LoadBindlessVertex(geometry.VertexBufferIndex, i0);
    const VertexAttributes v1 = LoadBindlessVertex(geometry.VertexBufferIndex, i1);
    const VertexAttributes v2 = LoadBindlessVertex(geometry.VertexBufferIndex, i2);
//Modify End

    const float3 barycentrics = float3(
        1.0f - hitBarycentrics.x - hitBarycentrics.y,
        hitBarycentrics.x,
        hitBarycentrics.y);

    float2 uv = v0.Uv.xy * barycentrics.x + v1.Uv.xy * barycentrics.y + v2.Uv.xy * barycentrics.z;
    uv = uv * material.TilingOffset.xy + material.TilingOffset.zw;

    const float3 p0Ws = mul(objectToWorld, float4(v0.Position.xyz, 1.0f));
    const float3 p1Ws = mul(objectToWorld, float4(v1.Position.xyz, 1.0f));
    const float3 p2Ws = mul(objectToWorld, float4(v2.Position.xyz, 1.0f));
    const float3 positionError = ComputeTrianglePositionError(p0Ws, p1Ws, p2Ws, barycentrics);
    float3 normalOs = normalize(v0.Normal.xyz * barycentrics.x + v1.Normal.xyz * barycentrics.y + v2.Normal.xyz * barycentrics.z);
    float3 normalWs = normalize(mul((float3x3)objectToWorld, normalOs));

    if (material.HasNormalMap != 0u)
    {
        const float3 tangentOs = normalize(v0.Tangent.xyz * barycentrics.x + v1.Tangent.xyz * barycentrics.y + v2.Tangent.xyz * barycentrics.z);
        const float3 bitangentOs = normalize(v0.Bitangent.xyz * barycentrics.x + v1.Bitangent.xyz * barycentrics.y + v2.Bitangent.xyz * barycentrics.z);
        const float3 tangentWs = normalize(mul((float3x3)objectToWorld, tangentOs));
        const float3 bitangentWs = normalize(mul((float3x3)objectToWorld, bitangentOs));
        const float3x3 tbn = float3x3(tangentWs, bitangentWs, normalWs);
//Modify Begin:2026-07-30 by BestHui
        const float3 normalTs = UnpackNormalMap(SampleBindlessTexture2DLevel(material.NormalTextureIndex, LinearWrapSampler, uv, 0.0f).xyz);
//Modify End
        normalWs = normalize(mul(normalTs, tbn));
    }

    normalWs = dot(normalWs, worldRayDirection) > 0.0f ? -normalWs : normalWs;

//Modify Begin:2026-07-30 by BestHui
    const float4 texel = material.HasDiffuseMap != 0u ? SampleBindlessTexture2DLevel(material.DiffuseTextureIndex, LinearWrapSampler, uv, 0.0f) : 1.0f;
//Modify End
    float metallic = material.Metallic;
    if (material.HasMetallicMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        metallic *= SampleBindlessTexture2DLevel(material.MetallicTextureIndex, LinearWrapSampler, uv, 0.0f).r;
//Modify End
    }

    float roughness = material.Roughness;
    if (material.HasRoughnessMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        roughness *= SampleBindlessTexture2DLevel(material.RoughnessTextureIndex, LinearWrapSampler, uv, 0.0f).r;
//Modify End
    }

    float ambientOcclusion = 1.0f;
    if (material.HasAmbientOcclusionMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        ambientOcclusion *= SampleBindlessTexture2DLevel(material.AmbientOcclusionTextureIndex, LinearWrapSampler, uv, 0.0f).r;
//Modify End
    }

    RayPayload payload;
    payload.Hit = 1u;
    payload.HitT = hitT;
    payload.Normal = normalWs;
    payload.BaseColor = material.Diffuse.rgb * texel.rgb;
    payload.PositionError = positionError;
    payload.Metallic = saturate(metallic);
    payload.Roughness = saturate(roughness);
    payload.AmbientOcclusion = saturate(ambientOcclusion);
    payload.Padding0 = 0u;
    return payload;
}

#endif
