//Modify Begin:2026-10-14 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SVGFCommon.hlsli"

cbuffer SVGFFilterConstants : register(b0)
{
    uint Width;
    uint Height;
    uint StepSize;
    uint Padding;
    float PhiColor;
    float PhiNormal;
    float PhiDepth;
    float Padding1;
};
Texture2D<float4> InputColor : register(t0);
Texture2D<float4> CurrentSurface : register(t1);
RWTexture2D<float4> OutputColor : register(u0);

float filteredVariance(int2 pixel)
{
    float sum = 0.0f;
    float sumWeight = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 p = pixel + int2(x, y);
            if (any(p < 0) || any(p >= int2(Width, Height))) continue;
            const float w = (x == 0 ? 0.5f : 0.25f) * (y == 0 ? 0.5f : 0.25f);
            sum += w * InputColor.Load(int3(p, 0)).a;
            sumWeight += w;
        }
    }
    return max(0.0f, sum / sumWeight);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pixel = int2(dispatchThreadId.xy);
    if (any(pixel >= int2(Width, Height))) return;
    const float4 center = InputColor.Load(int3(pixel, 0));
    const float4 surface = CurrentSurface.Load(int3(pixel, 0));
    if (surface.x <= 0.0f)
    {
        OutputColor[pixel] = center;
        return;
    }
    const float luma = SVGFLuminance(center.rgb);
    const float phiLuminance = PhiColor * sqrt(filteredVariance(pixel) + 1e-10f);
    const float kernel[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };
    float3 colorSum = center.rgb;
    float varianceSum = center.a;
    float weightSum = 1.0f;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            const int2 offset = int2(x, y);
            const int2 p = pixel + offset * int(StepSize);
            if ((x == 0 && y == 0) || any(p < 0) || any(p >= int2(Width, Height))) continue;
            const float4 sample = InputColor.Load(int3(p, 0));
            const float w = kernel[abs(x)] * kernel[abs(y)] *
                SVGFEdgeWeight(surface, CurrentSurface.Load(int3(p, 0)), length(float2(offset)),
                    float(StepSize), PhiDepth, PhiNormal, luma, SVGFLuminance(sample.rgb), phiLuminance);
            colorSum += w * sample.rgb;
            varianceSum += w * w * sample.a;
            weightSum += w;
        }
    }
    OutputColor[pixel] = float4(colorSum / weightSum, varianceSum / (weightSum * weightSum));
}
//Modify End
