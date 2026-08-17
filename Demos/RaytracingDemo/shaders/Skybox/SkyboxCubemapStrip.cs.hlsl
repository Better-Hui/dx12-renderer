//Modify Begin:2026-08-06 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Common/EnvironmentTexture.hlsli>
#include "../Scene/SceneCamera.hlsli"

Texture2D<float> DepthTexture : register(t0, space0);
Texture2D SkyboxTexture : register(t1, space0);
RWTexture2D<float4> SceneColor : register(u0, space0);

float3 BuildSkyboxDirection(uint2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(Camera_Width, Camera_Height);
    const float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 positionVs = mul(Camera_InverseProjection, float4(ndc, 1.0f, 1.0f));
    positionVs.xyz /= max(abs(positionVs.w), 0.000001f);
    return normalize(mul(Camera_InverseView, float4(normalize(positionVs.xyz), 0.0f)).xyz);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height || DepthTexture.Load(int3(pixel, 0)) < 0.999999f)
    {
        return;
    }

    const float3 skyColor = SkyboxTexture.SampleLevel(
        g_Common_LinearWrapSampler,
        FrameworkDirectionToHorizontalCubemapStripUv(BuildSkyboxDirection(pixel)),
        0.0f).rgb *
        Camera_SkyLight.ColorAndIntensity.rgb *
        Camera_SkyLight.ColorAndIntensity.w;
    SceneColor[pixel] = float4(skyColor, 1.0f);
}
//Modify End
