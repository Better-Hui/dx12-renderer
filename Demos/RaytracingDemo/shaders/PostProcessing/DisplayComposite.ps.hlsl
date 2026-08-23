#include <ShaderLibrary/Common/RootSignature.hlsli>

Texture2D<float4> SceneColor : register(t0, space0);

float4 main(float2 uv : TEXCOORD) : SV_Target
{
    return SceneColor.SampleLevel(g_Common_LinearClampSampler, uv, 0.0f);
}
