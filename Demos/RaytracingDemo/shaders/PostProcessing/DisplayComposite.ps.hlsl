#include <ShaderLibrary/Common/RootSignature.hlsli>

Texture2D<float4> SceneColor : register(t0, space0);

float3 ToneMap(float3 color)
{
    color = max(color, 0.0f);
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    const float4 sceneColor = SceneColor.SampleLevel(g_Common_LinearClampSampler, uv, 0.0f);
    return float4(ToneMap(sceneColor.rgb), sceneColor.a);
}
