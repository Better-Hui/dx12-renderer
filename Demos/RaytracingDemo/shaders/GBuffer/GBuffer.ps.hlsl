#include <ShaderLibrary/Common/RootSignature.hlsli>
//Modify Begin:2026-08-06 by BestHui
#include <Bindless/BindlessResources.hlsli>
//Modify End

struct PixelShaderInput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
//Modify Begin:2026-07-30 by BestHui
    nointerpolation uint MeshletDebugId : TEXCOORD3;
//Modify End
//Modify Begin:2026-07-29 by BestHui
    bool IsFrontFace : SV_IsFrontFace;
//Modify End
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

cbuffer MaterialCBuffer : register(b2)
{
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float4 TilingOffset;
//Modify Begin:2026-07-30 by BestHui
    uint DiffuseTextureIndex;
    uint NormalTextureIndex;
    uint MetallicTextureIndex;
    uint RoughnessTextureIndex;
    uint AmbientOcclusionTextureIndex;
    uint EmissionTextureIndex;
//Modify End
    float Metallic;
    float Roughness;
    uint HasDiffuseMap;
    uint HasNormalMap;
    uint HasMetallicMap;
    uint HasRoughnessMap;
    uint HasAmbientOcclusionMap;
    uint HasEmissionMap;
    uint Padding0;
    uint Padding1;
};

//Modify Begin:2026-07-30 by BestHui
cbuffer GBufferDebugCBuffer : register(b3)
{
    uint DebugMeshletClusters;
    uint3 GBufferDebugPadding;
};
//Modify End

float3 EncodeNormal(float3 normal)
{
    return normal * 0.5f + 0.5f;
}

float3 UnpackNormal(float3 normal)
{
    return normal * 2.0f - 1.0f;
}

float3 ApplyNormalMap(float3 normalWs, float3 tangentWs, float3 bitangentWs, float2 uv)
{
    const float3 tangent = normalize(tangentWs);
    const float3 bitangent = normalize(bitangentWs);
    const float3 normal = normalize(normalWs);
    const float3x3 tbn = float3x3(tangent, bitangent, normal);
//Modify Begin:2026-07-30 by BestHui
    const float3 normalTs = UnpackNormal(SampleBindlessTexture2D(NormalTextureIndex, g_Common_LinearWrapSampler, uv).xyz);
//Modify End
    return normalize(mul(normalTs, tbn));
}

//Modify Begin:2026-07-30 by BestHui
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
//Modify End

PixelShaderOutput main(PixelShaderInput IN)
{
    PixelShaderOutput OUT;

    const float2 uv = IN.Uv * TilingOffset.xy + TilingOffset.zw;
//Modify Begin:2026-07-30 by BestHui
    const float3 sampledDiffuse = HasDiffuseMap != 0u ? SampleBindlessTexture2D(DiffuseTextureIndex, g_Common_LinearWrapSampler, uv).rgb : 1.0f;
//Modify End
    const float3 baseColor = Diffuse.rgb * sampledDiffuse;
//Modify Begin:2026-07-30 by BestHui
    const float3 outputBaseColor = DebugMeshletClusters != 0u ? HashClusterColor(IN.MeshletDebugId) : baseColor;
//Modify End

    float3 normalWs = normalize(IN.NormalWs);
//Modify Begin:2026-07-29 by BestHui
    if (!IN.IsFrontFace)
    {
        normalWs = -normalWs;
    }
//Modify End
    if (HasNormalMap != 0u)
    {
        normalWs = ApplyNormalMap(normalWs, IN.TangentWs, IN.BitangentWs, uv);
    }

    float metallic = Metallic;
    if (HasMetallicMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        metallic *= SampleBindlessTexture2D(MetallicTextureIndex, g_Common_LinearWrapSampler, uv).r;
//Modify End
    }

    float roughness = Roughness;
    if (HasRoughnessMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        roughness *= SampleBindlessTexture2D(RoughnessTextureIndex, g_Common_LinearWrapSampler, uv).r;
//Modify End
    }

    float ambientOcclusion = 1.0f;
    if (HasAmbientOcclusionMap != 0u)
    {
//Modify Begin:2026-07-30 by BestHui
        ambientOcclusion *= SampleBindlessTexture2D(AmbientOcclusionTextureIndex, g_Common_LinearWrapSampler, uv).r;
//Modify End
    }

    metallic = saturate(metallic);
    roughness = saturate(roughness);
//Modify Begin:2026-07-30 by BestHui
    const float3 specularColor = lerp(Specular.rgb, outputBaseColor, metallic);

    OUT.AlbedoOcclusion = float4(outputBaseColor, ambientOcclusion);
//Modify End
    OUT.SpecularSmoothness = float4(specularColor, 1.0f - roughness);
    OUT.Normal = float4(EncodeNormal(normalWs), 1.0f);
//Modify Begin:2026-07-30 by BestHui
    const float3 emission = Emission.rgb * (HasEmissionMap != 0u
        ? SampleBindlessTexture2D(EmissionTextureIndex, g_Common_LinearWrapSampler, uv).rgb
        : 1.0f);
    OUT.EmissionMetallic = float4(emission, metallic);
//Modify End
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
