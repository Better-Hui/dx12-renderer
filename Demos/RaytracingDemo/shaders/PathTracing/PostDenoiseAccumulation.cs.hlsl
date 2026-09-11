//Modify Begin:2026-09-10 by Hui
RWTexture2D<float4> SceneColor;
RWTexture2D<float4> HistoryColor;

cbuffer PostDenoiseAccumulationConstants
{
    uint Width;
    uint Height;
    uint PreviousSampleCount;
    uint Padding;
};

float3 SanitizeRadiance(const float3 color)
{
    if (!all(isfinite(color)))
    {
        return 0.0f;
    }
    return min(max(color, 0.0f), 250.0f);
}

[numthreads(8, 8, 1)]
void main(const uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    const float3 currentColor = SanitizeRadiance(SceneColor[pixel].rgb);
    float3 accumulatedColor = currentColor;
    if (PreviousSampleCount > 0u)
    {
        const float3 historyColor = SanitizeRadiance(HistoryColor[pixel].rgb);
        accumulatedColor =
            (historyColor * float(PreviousSampleCount) + currentColor) /
            float(PreviousSampleCount + 1u);
    }

    HistoryColor[pixel] = float4(accumulatedColor, float(PreviousSampleCount + 1u));
    SceneColor[pixel] = float4(accumulatedColor, 1.0f);
}
//Modify End
