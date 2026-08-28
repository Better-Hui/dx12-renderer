//Modify Begin:2026-08-28 by Hui
Texture2D<float4> SourceColor;
Texture2D<float> AdaptedLuminance;
RWTexture2D<float4> OutputColor;

cbuffer AutoExposureConstants
{
    uint InputWidth;
    uint InputHeight;
    uint OutputWidth;
    uint OutputHeight;
    float MinLogLuminance;
    float LogLuminanceRange;
    float DeltaTime;
    float Tau;
    float ExposureScale;
    uint ExposureEnabled;
    uint OutputMode;
    uint Padding;
};

float3 ToneMap(float3 color)
{
    color = max(color, 0.0f);
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= OutputWidth || dispatchThreadId.y >= OutputHeight)
    {
        return;
    }

    uint2 sourcePixel = dispatchThreadId;
    if (InputWidth != OutputWidth || InputHeight != OutputHeight)
    {
        sourcePixel = uint2(
            min((uint)((dispatchThreadId.x + 0.5f) * InputWidth / (float)OutputWidth), InputWidth - 1u),
            min((uint)((dispatchThreadId.y + 0.5f) * InputHeight / (float)OutputHeight), InputHeight - 1u));
    }

    const float4 sourceColor = SourceColor[sourcePixel];
    const float adaptedLuminance = max(AdaptedLuminance[uint2(0u, 0u)], 0.0001f);
    const float exposure = ExposureEnabled != 0u
        ? ExposureScale / (9.6f * adaptedLuminance + 0.0001f)
        : 1.0f;
    const float3 exposedColor = max(sourceColor.rgb * exposure, 0.0f);
    OutputColor[dispatchThreadId] = OutputMode == 1u
        ? float4(exposedColor, sourceColor.a)
        : float4(ToneMap(exposedColor), sourceColor.a);
}
//Modify End
