#include <ShaderLibrary/Common/RootSignature.hlsli>

struct PixelShaderInput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
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

cbuffer MaterialCBuffer : register(b0)
{
    float4 Diffuse;
    float4 Specular;
    float4 TilingOffset;
    float Metallic;
    float Roughness;
    uint HasDiffuseMap;
    uint HasNormalMap;
    uint HasMetallicMap;
    uint HasRoughnessMap;
    uint HasAmbientOcclusionMap;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MetallicTexture : register(t2);
Texture2D RoughnessTexture : register(t3);
Texture2D AmbientOcclusionTexture : register(t4);

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
    const float3 normalTs = UnpackNormal(NormalTexture.Sample(g_Common_LinearWrapSampler, uv).xyz);
    return normalize(mul(normalTs, tbn));
}

PixelShaderOutput main(PixelShaderInput IN)
{
    PixelShaderOutput OUT;

    const float2 uv = IN.Uv * TilingOffset.xy + TilingOffset.zw;
    const float3 sampledDiffuse = HasDiffuseMap != 0u ? DiffuseTexture.Sample(g_Common_LinearWrapSampler, uv).rgb : 1.0f;
    const float3 baseColor = Diffuse.rgb * sampledDiffuse;

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
        metallic *= MetallicTexture.Sample(g_Common_LinearWrapSampler, uv).r;
    }

    float roughness = Roughness;
    if (HasRoughnessMap != 0u)
    {
        roughness *= RoughnessTexture.Sample(g_Common_LinearWrapSampler, uv).r;
    }

    float ambientOcclusion = 1.0f;
    if (HasAmbientOcclusionMap != 0u)
    {
        ambientOcclusion *= AmbientOcclusionTexture.Sample(g_Common_LinearWrapSampler, uv).r;
    }

    metallic = saturate(metallic);
    roughness = saturate(roughness);
    const float3 specularColor = lerp(Specular.rgb, baseColor, metallic);

    OUT.AlbedoOcclusion = float4(baseColor, ambientOcclusion);
    OUT.SpecularSmoothness = float4(specularColor, 1.0f - roughness);
    OUT.Normal = float4(EncodeNormal(normalWs), 1.0f);
    OUT.EmissionMetallic = float4(0.0f, 0.0f, 0.0f, metallic);
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
