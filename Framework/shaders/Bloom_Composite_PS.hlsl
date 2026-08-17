//Modify Begin:2026-08-17 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>

struct PixelShaderInput
{
    float2 Uv : TEXCOORD;
};

Texture2D sourceColorTexture : register(t0);
Texture2D bloomTexture : register(t1);

#include <ShaderLibrary/Blur.hlsli>

struct Parameters
{
    float2 TexelSize;
    float Intensity;
    float Padding;
};

ConstantBuffer<Parameters> parametersCb : register(b0);

float4 main(PixelShaderInput IN) : SV_TARGET
{
    const float3 sourceColor = sourceColorTexture.Sample(g_Common_LinearClampSampler, IN.Uv).rgb;
    const float3 bloom = BoxBlur(bloomTexture, g_Common_LinearClampSampler, IN.Uv, 1.0f, parametersCb.TexelSize);
    return float4(sourceColor + bloom, 1.0f);
}
//Modify End
