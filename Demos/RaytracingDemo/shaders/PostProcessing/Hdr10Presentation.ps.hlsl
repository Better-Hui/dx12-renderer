//Modify Begin:2026-08-28 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>

Texture2D<float4> SceneColor : register(t0, space0);

cbuffer HDR10PresentationConstants : register(b0)
{
    float PeakNits;
    float3 Padding;
};

float3 ToneMapToDisplayNits(const float3 color)
{
    const float3 positiveColor = max(color, 0.0f);
    return (positiveColor / (positiveColor + 1.0f)) * PeakNits;
}

float3 Rec709ToRec2020(const float3 color)
{
    return float3(
        dot(color, float3(0.6274f, 0.3293f, 0.0433f)),
        dot(color, float3(0.0691f, 0.9195f, 0.0114f)),
        dot(color, float3(0.0164f, 0.0880f, 0.8956f)));
}

float3 EncodePQ(const float3 luminanceNits)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 128.0f;
    const float c3 = 2392.0f / 128.0f;
    const float3 normalized = saturate(luminanceNits / 10000.0f);
    const float3 powered = pow(normalized, m1);
    return pow((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
}

float4 main(const float2 uv : TEXCOORD) : SV_Target
{
    const float3 displayNits = ToneMapToDisplayNits(
        SceneColor.SampleLevel(g_Common_LinearClampSampler, uv, 0.0f).rgb);
    return float4(EncodePQ(Rec709ToRec2020(displayNits)), 1.0f);
}
//Modify End
