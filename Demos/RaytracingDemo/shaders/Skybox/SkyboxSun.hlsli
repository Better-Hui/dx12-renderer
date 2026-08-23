//Modify Begin:2026-08-23 by Hui
#ifndef RAYTRACING_DEMO_SKYBOX_SUN_HLSLI
#define RAYTRACING_DEMO_SKYBOX_SUN_HLSLI

cbuffer SkyboxSunConstants : register(b1, space0)
{
    float4 SkyboxSun_DirectionAndPadding;
    float4 SkyboxSun_ColorAndIntensity;
};

float3 AddSkyboxSunDisk(const float3 skyColor, const float3 directionWs)
{
    const float intensity = SkyboxSun_ColorAndIntensity.w;

    const float viewToSun = saturate(dot(directionWs, SkyboxSun_DirectionAndPadding.xyz));
    const float sigma = 0.02f;
    const float disk = exp((viewToSun - 1.0f) / (sigma * sigma));
    return skyColor + SkyboxSun_ColorAndIntensity.rgb * intensity * disk;
}

#endif
//Modify End
