//Modify Begin:2026-10-14 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SVGFCommon.hlsli"

cbuffer SVGFPrepareConstants : register(b0)
{
    row_major matrix WorldToView;
    uint Width;
    uint Height;
    uint2 Padding;
};
Texture2D<float4> NoisyRadiance : register(t0);
Texture2D<float4> GBufferNormal : register(t1);
Texture2D<float4> GBufferPosition : register(t2);
Texture2D<float> DepthTexture : register(t3);
Texture2D<float4> GBufferAlbedoOcclusion : register(t4);
Texture2D<float4> GBufferEmissionMetallic : register(t5);
RWTexture2D<float4> Illumination : register(u0);
RWTexture2D<float4> CurrentSurface : register(u1);

float loadViewZ(int2 pixel, float fallback)
{
    if (any(pixel < 0) || any(pixel >= int2(Width, Height))) return fallback;
    if (!SVGFIsValidDepth(DepthTexture.Load(int3(pixel, 0)))) return fallback;
    const float z = mul(float4(GBufferPosition.Load(int3(pixel, 0)).xyz, 1.0f), WorldToView).z;
    return isfinite(z) && z > 0.0f ? z : fallback;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pixel = int2(dispatchThreadId.xy);
    if (any(pixel >= int2(Width, Height))) return;
    const float depth = DepthTexture.Load(int3(pixel, 0));
    const float3 position = GBufferPosition.Load(int3(pixel, 0)).xyz;
    const float3 encodedNormal = GBufferNormal.Load(int3(pixel, 0)).xyz;
    const float z = mul(float4(position, 1.0f), WorldToView).z;
    if (!SVGFIsValidDepth(depth) || !all(isfinite(position)) ||
        !all(isfinite(encodedNormal)) || !isfinite(z) || z <= 0.0f)
    {
        Illumination[pixel] = 0.0f;
        CurrentSurface[pixel] = float4(-1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // Prefer one-sided gradients that do not cross a silhouette.
    const float dx = min(abs(loadViewZ(pixel + int2(-1, 0), z) - z),
        abs(loadViewZ(pixel + int2(1, 0), z) - z));
    const float dy = min(abs(loadViewZ(pixel + int2(0, -1), z) - z),
        abs(loadViewZ(pixel + int2(0, 1), z) - z));
    CurrentSurface[pixel] = float4(z, dx + dy,
        SVGFEncodeNormal(DecodeSVGFNormal(encodedNormal)));
    const float4 material = GBufferEmissionMetallic.Load(int3(pixel, 0));
    const float3 factor = SVGFDemodulation(GBufferAlbedoOcclusion.Load(int3(pixel, 0)), material);
    Illumination[pixel] = float4(SVGFSanitize(NoisyRadiance.Load(int3(pixel, 0)).rgb
        - SVGFSanitize(material.rgb)) / factor, 0.0f);
}
//Modify End
