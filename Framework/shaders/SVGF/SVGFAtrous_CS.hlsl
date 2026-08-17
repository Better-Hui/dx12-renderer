//Modify Begin:2026-07-27 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SVGFCommon.hlsli"

cbuffer SVGFAtrousConstants : register(b0)
{
    uint Width;
    uint Height;
    uint StepSize;
//Modify Begin:2026-07-27 by Hui
    uint Direction;
//Modify End
    float PhiColor;
    float PhiNormal;
    float PhiDepth;
    float Padding1;
};

Texture2D<float4> InputColor : register(t0);
Texture2D<float> Variance : register(t1);
Texture2D<float4> GBufferNormal : register(t2);
Texture2D<float4> GBufferPosition : register(t3);
Texture2D<float> DepthTexture : register(t4);

RWTexture2D<float4> OutputColor : register(u0);

static const float Kernel[5] = {
    1.0f / 16.0f,
    1.0f / 4.0f,
    3.0f / 8.0f,
    1.0f / 4.0f,
    1.0f / 16.0f
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    const float centerDepth = DepthTexture.Load(int3(pixel, 0));
    const float4 centerColor = InputColor.Load(int3(pixel, 0));
    if (!SVGFIsValidDepth(centerDepth))
    {
        OutputColor[pixel] = centerColor;
        return;
    }

    const float3 centerNormal = DecodeSVGFNormal(GBufferNormal.Load(int3(pixel, 0)).xyz);
    const float3 centerPosition = GBufferPosition.Load(int3(pixel, 0)).xyz;
    const float centerVariance = Variance.Load(int3(pixel, 0));

    float3 colorSum = 0.0f;
    float weightSum = 0.0f;

//Modify Begin:2026-07-27 by Hui
    [unroll]
    for (int tap = -2; tap <= 2; ++tap)
    {
        const int2 tapOffset = Direction == 0u ? int2(tap, 0) : int2(0, tap);
        const int2 samplePixel = int2(pixel) + tapOffset * int(StepSize);
        if (samplePixel.x < 0 || samplePixel.y < 0 || samplePixel.x >= int(Width) || samplePixel.y >= int(Height))
        {
            continue;
        }

        const float sampleDepth = DepthTexture.Load(int3(samplePixel, 0));
        if (!SVGFIsValidDepth(sampleDepth))
        {
            continue;
        }

        const float4 sampleColor = InputColor.Load(int3(samplePixel, 0));
        const float3 sampleNormal = DecodeSVGFNormal(GBufferNormal.Load(int3(samplePixel, 0)).xyz);
        const float3 samplePosition = GBufferPosition.Load(int3(samplePixel, 0)).xyz;

        const float kernelWeight = Kernel[tap + 2];
        const float normalWeight = SVGFNormalWeight(centerNormal, sampleNormal, PhiNormal);
        const float depthWeight = SVGFDepthWeight(centerDepth, sampleDepth, centerPosition, samplePosition, PhiDepth);
        const float colorWeight = SVGFColorWeight(centerColor.rgb, sampleColor.rgb, centerVariance, PhiColor);
        const float weight = kernelWeight * normalWeight * depthWeight * colorWeight;

        colorSum += sampleColor.rgb * weight;
        weightSum += weight;
    }
//Modify End

    const float3 filteredColor = weightSum > 0.0f ? colorSum / weightSum : centerColor.rgb;
    OutputColor[pixel] = float4(filteredColor, centerColor.a);
}
//Modify End
