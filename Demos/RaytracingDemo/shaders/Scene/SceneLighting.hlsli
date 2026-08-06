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

struct AreaLightData
{
    float4 PositionAndRange;
    float4 NormalAndType;
    float4 AxisUAndExtent;
    float4 AxisVAndExtent;
    float4 ColorAndIntensity;
//Modify Begin:2026-08-06 by BestHui
    float4 EmissiveUv0Uv1;
    float4 EmissiveUv2AndMaterialIndex;
//Modify End
};

//Modify Begin:2026-08-06 by BestHui
static const float AreaLightType_Rectangle = 0.0f;
static const float AreaLightType_EmissiveTriangle = 1.0f;
//Modify End

#endif
