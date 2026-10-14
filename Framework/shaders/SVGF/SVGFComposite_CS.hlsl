//Modify Begin:2026-10-14 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SVGFCommon.hlsli"

cbuffer SVGFCompositeConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> FilteredColor : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float4> GBufferAlbedoOcclusion : register(t2);
Texture2D<float4> GBufferEmissionMetallic : register(t3);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    if (!SVGFIsValidDepth(DepthTexture.Load(int3(pixel, 0))))
    {
        return;
    }

    const float4 material = GBufferEmissionMetallic.Load(int3(pixel, 0));
    const float3 factor = SVGFDemodulation(GBufferAlbedoOcclusion.Load(int3(pixel, 0)), material);
    const float3 color = SVGFSanitize(FilteredColor.Load(int3(pixel, 0)).rgb) * factor
        + SVGFSanitize(material.rgb);
    Output[pixel] = float4(color, 1.0f);
}
//Modify End
