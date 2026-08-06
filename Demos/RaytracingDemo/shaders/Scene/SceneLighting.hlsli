#ifndef RAYTRACING_DEMO_SCENE_LIGHTING_HLSLI
#define RAYTRACING_DEMO_SCENE_LIGHTING_HLSLI

struct SkyLightData
{
    float4 ColorAndIntensity;
};

struct DirectionalLightData
{
    float4 DirectionAndAngularRadius;
    float4 ColorAndIntensity;
};

struct PointLightData
{
    float4 PositionAndRange;
    float4 ColorAndIntensity;
    float4 Attenuation;
};

#endif
