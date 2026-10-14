//Modify Begin:2026-10-14 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SVGFCommon.hlsli"

cbuffer SVGFTemporalConstants : register(b0)
{
    row_major matrix PreviousWorldToView;
    uint Width;
    uint Height;
    uint ResetHistory;
    uint Padding;
    float TemporalAlpha;
    float MomentsAlpha;
    float2 Padding1;
};
Texture2D<float4> Illumination : register(t0);
Texture2D<float4> CurrentSurface : register(t1);
Texture2D<float4> GBufferPosition : register(t2);
Texture2D<float2> MotionVector : register(t3);
Texture2D<float4> HistoryColor : register(t4);
Texture2D<float2> HistoryMoments : register(t5);
Texture2D<float4> HistorySurface : register(t6);
Texture2D<float> HistoryLength : register(t7);
RWTexture2D<float4> TemporalColor : register(u0);
RWTexture2D<float2> OutHistoryMoments : register(u1);
RWTexture2D<float> OutHistoryLength : register(u2);

float normalDifference(int2 pixel, int2 offset, float3 normal)
{
    const int2 p = pixel + offset;
    if (any(p < 0) || any(p >= int2(Width, Height))) return 0.0f;
    const float4 surface = CurrentSurface.Load(int3(p, 0));
    return surface.x > 0.0f ? distance(normal, SVGFDecodeNormal(surface.zw)) : 0.0f;
}

bool historyCompatible(int2 p, float previousZ, float depthFootprint,
    float3 normal, float normalFootprint)
{
    if (any(p < 0) || any(p >= int2(Width, Height))) return false;
    const float4 surface = HistorySurface.Load(int3(p, 0));
    if (surface.x <= 0.0f || !all(isfinite(surface)) ||
        HistoryLength.Load(int3(p, 0)) < 1.0f) return false;
    const float toleranceZ = 10.0f * (max(depthFootprint, surface.y) + 0.01f);
    const float toleranceNormal = min(0.5f, 16.0f * (normalFootprint + 0.01f));
    return abs(surface.x - previousZ) <= toleranceZ &&
        distance(normal, SVGFDecodeNormal(surface.zw)) <= toleranceNormal &&
        all(isfinite(HistoryColor.Load(int3(p, 0)))) &&
        all(isfinite(HistoryMoments.Load(int3(p, 0))));
}

bool loadHistory(int2 pixel, float4 surface, out float3 color,
    out float2 moments, out float historyLength)
{
    color = 0.0f;
    moments = 0.0f;
    historyLength = 0.0f;
    if (ResetHistory != 0u) return false;
    const float2 motion = MotionVector.Load(int3(pixel, 0));
    if (!all(isfinite(motion))) return false;
    const float2 previousPixel = float2(pixel) + motion * float2(Width, Height);
    if (any(previousPixel < -0.5f) || any(previousPixel > float2(Width, Height) - 0.5f)) return false;
    // Evaluate this point in the previous camera so camera translation does
    // not invalidate otherwise compatible history.
    const float3 position = GBufferPosition.Load(int3(pixel, 0)).xyz;
    const float previousZ = mul(float4(position, 1.0f), PreviousWorldToView).z;
    if (!isfinite(previousZ) || previousZ <= 0.0f) return false;
    const float3 normal = SVGFDecodeNormal(surface.zw);
    const float normalFootprint =
        min(normalDifference(pixel, int2(-1, 0), normal), normalDifference(pixel, int2(1, 0), normal)) +
        min(normalDifference(pixel, int2(0, -1), normal), normalDifference(pixel, int2(0, 1), normal));
    const int2 base = int2(floor(previousPixel));
    const float2 f = frac(previousPixel);
    const int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    const float weights[4] = { (1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y),
        (1.0f - f.x) * f.y, f.x * f.y };
    float sumWeight = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const int2 p = base + offsets[i];
        if (historyCompatible(p, previousZ, surface.y, normal, normalFootprint))
        {
            color += weights[i] * HistoryColor.Load(int3(p, 0)).rgb;
            moments += weights[i] * HistoryMoments.Load(int3(p, 0));
            historyLength += weights[i] * HistoryLength.Load(int3(p, 0));
            sumWeight += weights[i];
        }
    }
    if (sumWeight < 0.01f)
    {
        color = 0.0f;
        moments = 0.0f;
        historyLength = 0.0f;
        sumWeight = 0.0f;
        const int2 nearest = int2(floor(previousPixel + 0.5f));
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                const int2 p = nearest + int2(x, y);
                if (historyCompatible(p, previousZ, surface.y, normal, normalFootprint))
                {
                    color += HistoryColor.Load(int3(p, 0)).rgb;
                    moments += HistoryMoments.Load(int3(p, 0));
                    historyLength += HistoryLength.Load(int3(p, 0));
                    sumWeight += 1.0f;
                }
            }
        }
    }
    if (sumWeight <= 0.0f) return false;
    color /= sumWeight;
    moments /= sumWeight;
    historyLength /= sumWeight;
    return true;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pixel = int2(dispatchThreadId.xy);
    if (any(pixel >= int2(Width, Height))) return;
    const float4 surface = CurrentSurface.Load(int3(pixel, 0));
    if (surface.x <= 0.0f)
    {
        TemporalColor[pixel] = 0.0f;
        OutHistoryMoments[pixel] = 0.0f;
        OutHistoryLength[pixel] = 0.0f;
        return;
    }
    const float3 current = Illumination.Load(int3(pixel, 0)).rgb;
    const float luma = SVGFLuminance(current);
    float3 previousColor;
    float2 previousMoments;
    float historyLength;
    const bool valid = loadHistory(pixel, surface, previousColor, previousMoments, historyLength);
    historyLength = valid ? min(historyLength + 1.0f, 32.0f) : 1.0f;
    // Alpha specifies the minimum CURRENT-frame contribution. Short histories
    // use equal sample weights; disocclusions restart with the current sample.
    const float alpha = valid ? max(TemporalAlpha, rcp(historyLength)) : 1.0f;
    const float momentsAlpha = valid ? max(MomentsAlpha, rcp(historyLength)) : 1.0f;
    const float2 moments = lerp(previousMoments, float2(luma, luma * luma), momentsAlpha);
    const float variance = max(0.0f, moments.y - moments.x * moments.x);
    TemporalColor[pixel] = float4(lerp(previousColor, current, alpha), variance);
    OutHistoryMoments[pixel] = moments;
    OutHistoryLength[pixel] = historyLength;
}
//Modify End
