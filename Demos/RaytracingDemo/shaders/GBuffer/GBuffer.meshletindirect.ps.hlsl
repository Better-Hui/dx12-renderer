//Modify Begin:2026-08-26 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Bindless/BindlessResources.hlsli>
#include "../Scene/SceneGeometry.hlsli"

struct PixelShaderInput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
    nointerpolation uint MeshletDebugId : TEXCOORD3;
    nointerpolation uint MaterialIndex : TEXCOORD4;
    nointerpolation uint DebugMeshletClusters : TEXCOORD5;
    bool IsFrontFace : SV_IsFrontFace;
};

struct PixelShaderOutput
{
    float4 AlbedoOcclusion : SV_TARGET0;
    float4 SpecularSmoothness : SV_TARGET1;
    float4 Normal : SV_TARGET2;
    float4 EmissionMetallic : SV_TARGET3;
    float4 Position : SV_TARGET4;
    float2 MotionVector : SV_TARGET5;
};

StructuredBuffer<MaterialData> MeshletMaterials : register(t5, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

float3 EncodeNormal(float3 normal)
{
    return normal * 0.5f + 0.5f;
}

float3 UnpackNormal(float3 normal)
{
    return normal * 2.0f - 1.0f;
}

float3 HashClusterColor(uint id)
{
    id ^= id >> 16;
    id *= 0x7feb352du;
    id ^= id >> 15;
    id *= 0x846ca68bu;
    id ^= id >> 16;

    const float3 color = float3(
        ((id >> 0) & 255u) / 255.0f,
        ((id >> 8) & 255u) / 255.0f,
        ((id >> 16) & 255u) / 255.0f);
    return lerp(color, float3(1.0f, 1.0f, 1.0f), 0.15f);
}

float3 ApplyNormalMap(MaterialData material, float3 normalWs, float3 tangentWs, float3 bitangentWs, float2 uv)
{
    const float3 tangent = normalize(tangentWs);
    const float3 bitangent = normalize(bitangentWs);
    const float3 normal = normalize(normalWs);
    const float3x3 tbn = float3x3(tangent, bitangent, normal);
    const float3 normalTs = UnpackNormal(SampleBindlessTexture2D(material.NormalTextureIndex, g_Common_LinearWrapSampler, uv).xyz);
    return normalize(mul(normalTs, tbn));
}

PixelShaderOutput main(PixelShaderInput IN)
{
    const MaterialData material = MeshletMaterials[IN.MaterialIndex];
    const float2 uv = IN.Uv * material.TilingOffset.xy + material.TilingOffset.zw;
    const float3 sampledDiffuse = material.HasDiffuseMap != 0u ? SampleBindlessTexture2D(material.DiffuseTextureIndex, g_Common_LinearWrapSampler, uv).rgb : 1.0f;
    const float3 baseColor = material.Diffuse.rgb * sampledDiffuse;
    const float3 outputBaseColor = IN.DebugMeshletClusters != 0u ? HashClusterColor(IN.MeshletDebugId) : baseColor;

    float3 normalWs = normalize(IN.NormalWs);
    if (!IN.IsFrontFace)
    {
        normalWs = -normalWs;
    }
    if (material.HasNormalMap != 0u)
    {
        normalWs = ApplyNormalMap(material, normalWs, IN.TangentWs, IN.BitangentWs, uv);
    }

    float metallic = material.Metallic;
    if (material.HasMetallicMap != 0u)
    {
        metallic *= SampleBindlessTexture2D(material.MetallicTextureIndex, g_Common_LinearWrapSampler, uv).r;
    }

    float roughness = material.Roughness;
    if (material.HasRoughnessMap != 0u)
    {
        roughness *= SampleBindlessTexture2D(material.RoughnessTextureIndex, g_Common_LinearWrapSampler, uv).r;
    }

    float ambientOcclusion = 1.0f;
    if (material.HasAmbientOcclusionMap != 0u)
    {
        ambientOcclusion *= SampleBindlessTexture2D(material.AmbientOcclusionTextureIndex, g_Common_LinearWrapSampler, uv).r;
    }

    metallic = saturate(metallic);
    roughness = saturate(roughness);
    const float3 specularColor = lerp(material.Specular.rgb, outputBaseColor, metallic);

    PixelShaderOutput OUT;
    OUT.AlbedoOcclusion = float4(outputBaseColor, ambientOcclusion);
    OUT.SpecularSmoothness = float4(specularColor, 1.0f - roughness);
    OUT.Normal = float4(EncodeNormal(normalWs), 1.0f);
    const float3 emission = material.Emission.rgb * (material.HasEmissionMap != 0u
        ? SampleBindlessTexture2D(material.EmissionTextureIndex, g_Common_LinearWrapSampler, uv).rgb
        : 1.0f);
    OUT.EmissionMetallic = float4(emission, metallic);
    OUT.Position = float4(IN.PositionWs, 1.0f);
    if (IN.CurrentPositionCs.w <= 0.0001f || IN.PreviousPositionCs.w <= 0.0001f)
    {
        OUT.MotionVector = 0.0f;
        return OUT;
    }

    const float2 currentNdc = IN.CurrentPositionCs.xy / IN.CurrentPositionCs.w;
    const float2 previousNdc = IN.PreviousPositionCs.xy / IN.PreviousPositionCs.w;
    const float2 currentUv = currentNdc * float2(0.5f, -0.5f) + 0.5f;
    const float2 previousUv = previousNdc * float2(0.5f, -0.5f) + 0.5f;
    OUT.MotionVector = previousUv - currentUv;
    return OUT;
}
//Modify End
