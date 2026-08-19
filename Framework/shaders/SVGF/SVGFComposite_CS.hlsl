//Modify Begin:2026-07-28 by Hui
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

    const float3 color = max(FilteredColor.Load(int3(pixel, 0)).rgb, 0.0f);
    Output[pixel] = float4(color, 1.0f);
}
//Modify End
