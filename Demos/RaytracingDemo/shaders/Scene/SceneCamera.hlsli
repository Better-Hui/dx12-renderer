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
    uint Camera_SamplesPerPixel;
    uint Camera_DirectionalLightCount;
    uint Camera_PointLightCount;
//Modify Begin:2026-08-26 by Hui
    uint Camera_SpotLightCount;
//Modify End
    uint Camera_SurfaceEmitterCount;
    uint Camera_FrameIndex;
    uint Camera_AccumulationFrameIndex;
    uint Camera_AccumulationEnabled;
    uint Camera_NRDDenoiserMode;
//Modify Begin:2026-08-13 by Hui
    uint Camera_PaddingBeforeNrdParameters0;
    uint Camera_PaddingBeforeNrdParameters1;
//Modify End
    float4 Camera_NRDReblurHitDistanceParameters;
//Modify Begin:2026-08-05 by Hui
    uint Camera_ReSTIRDIHistoryValid;
//Modify End
    uint Camera_UseSolidSkyFallback;
    uint Camera_Padding1;
    uint Camera_Padding2;
    SkyLightData Camera_SkyLight;
};

#endif
