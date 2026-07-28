//Modify Begin:2026-07-27 by BestHui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "../../../External/NRD/Shaders/NRD.hlsli"

cbuffer NRDOutputCompositeConstants : register(b0)
{
    uint Width;
    uint Height;
    uint DenoiserMode;
    uint Padding1;
};

Texture2D<float4> DenoisedRadiance : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float4> GBufferAlbedoOcclusion : register(t2);
Texture2D<float4> GBufferEmissionMetallic : register(t3);
RWTexture2D<float4> Output : register(u0);

float3 GetNRDDiffuseDemodulation(uint2 pixel)
{
    float3 diffuse = saturate(GBufferAlbedoOcclusion.Load(int3(pixel, 0)).rgb);
    float metallic = saturate(GBufferEmissionMetallic.Load(int3(pixel, 0)).a);
    float3 diffuseFactor = max(diffuse * (1.0f - metallic), 0.05f);
    return lerp(diffuseFactor, float3(1.0f, 1.0f, 1.0f), metallic);
}

//Modify Begin:2026-07-28 by BestHui
float3 SanitizeNRDCompositeColor(float3 color)
{
    if (!all(isfinite(color)))
    {
        return 0.0f;
    }

    return min(max(color, 0.0f), 250.0f);
}
//Modify End

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    if (DepthTexture.Load(int3(pixel, 0)) >= 1.0f)
    {
        return;
    }

    float4 denoised = DenoisedRadiance.Load(int3(pixel, 0));
    if (DenoiserMode == 1u)
    {
        denoised = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(denoised);
    }

//Modify Begin:2026-07-28 by BestHui
    float3 color = SanitizeNRDCompositeColor(denoised.rgb * GetNRDDiffuseDemodulation(pixel));
    Output[pixel] = float4(color, 1.0f);
//Modify End
}
//Modify End
