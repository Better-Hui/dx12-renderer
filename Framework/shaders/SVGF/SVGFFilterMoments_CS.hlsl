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
Texture2D<float2> Moments : register(t1);
Texture2D<float> HistoryLength : register(t2);
Texture2D<float4> CurrentSurface : register(t3);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pixel = int2(dispatchThreadId.xy);
    if (any(pixel >= int2(Width, Height))) return;
    const float4 center = InputColor.Load(int3(pixel, 0));
    const float4 surface = CurrentSurface.Load(int3(pixel, 0));
    const float h = HistoryLength.Load(int3(pixel, 0));
    if (h >= 4.0f || surface.x <= 0.0f)
    {
        OutputColor[pixel] = center;
        return;
    }
    const float luma = SVGFLuminance(center.rgb);
    float3 colorSum = center.rgb;
    float2 momentsSum = Moments.Load(int3(pixel, 0));
    float weightSum = 1.0f;
    for (int y = -3; y <= 3; ++y)
    {
        for (int x = -3; x <= 3; ++x)
        {
            const int2 offset = int2(x, y);
            const int2 p = pixel + offset;
            if ((x == 0 && y == 0) || any(p < 0) || any(p >= int2(Width, Height))) continue;
            const float3 color = InputColor.Load(int3(p, 0)).rgb;
            // Bootstrap variance spatially without using zero sample variance
            // as an edge-stopping signal.
            const float w = SVGFEdgeWeight(surface, CurrentSurface.Load(int3(p, 0)),
                length(float2(offset)), 3.0f, PhiDepth, PhiNormal, luma,
                SVGFLuminance(color), PhiColor);
            colorSum += w * color;
            momentsSum += w * Moments.Load(int3(p, 0));
            weightSum += w;
        }
    }
    const float2 moments = momentsSum / weightSum;
    const float variance = max(0.0f, moments.y - moments.x * moments.x) * 4.0f / max(h, 1.0f);
    OutputColor[pixel] = float4(colorSum / weightSum, variance);
}
//Modify End
