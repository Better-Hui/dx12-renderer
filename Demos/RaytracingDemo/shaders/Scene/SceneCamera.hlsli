#ifndef RAYTRACING_DEMO_SCENE_CAMERA_HLSLI
#define RAYTRACING_DEMO_SCENE_CAMERA_HLSLI

#include "SceneLighting.hlsli"

cbuffer CameraConstants : register(b0, space0)
{
    matrix Camera_InverseView;
    matrix Camera_InverseProjection;
    float4 Camera_Position;
    uint Camera_Width;
    uint Camera_Height;
    uint Camera_MaxBounces;
    uint Camera_SamplesPerPixel;
    uint Camera_DirectionalLightCount;
    uint Camera_PointLightCount;
    uint Camera_AreaLightCount;
    uint Camera_FrameIndex;
    uint Camera_AccumulationFrameIndex;
    uint Camera_AccumulationEnabled;
    uint Camera_NRDDenoiserMode;
    uint Camera_Padding0;
    float4 Camera_NRDReblurHitDistanceParameters;
    uint Camera_DirectLightingEnabled;
    uint Camera_IndirectLightingEnabled;
    uint Camera_Padding1;
    uint Camera_Padding2;
    SkyLightData Camera_SkyLight;
};

#endif
