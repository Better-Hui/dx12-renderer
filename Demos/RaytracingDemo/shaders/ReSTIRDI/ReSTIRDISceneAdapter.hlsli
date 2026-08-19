//Modify Begin:2026-08-06 by Hui
#ifndef RAYTRACING_DEMO_RESTIR_DI_SCENE_ADAPTER_HLSLI
#define RAYTRACING_DEMO_RESTIR_DI_SCENE_ADAPTER_HLSLI

#define FRAMEWORK_RESTIR_DI_SCENE_ADAPTER 1

#include "../PathTracing/PathTracing.rayquery.hlsli"
#include "../PathTracing/PathTracingShared.hlsli"

#define ReSTIRDI_Surface SurfaceData
#define ReSTIRDI_LightSample PathTracingDirectLightSample
#define ReSTIRDI_LoadSurface LoadGBufferSurface
#define ReSTIRDI_GetLightCount GetReSTIRDILightCount
#define ReSTIRDI_SampleLightIndex SamplePathTracingDirectLightIndex
#define ReSTIRDI_SampleLight SamplePathTracingDirectLight
#define ReSTIRDI_TestVisibility IsPathTracingDirectLightSampleVisible
#define ReSTIRDI_Luminance Luminance
#define ReSTIRDI_Random01 Random01
#define ReSTIRDI_InitializeRandomState InitializeRandomState
#define ReSTIRDI_ScreenWidth Camera_Width
#define ReSTIRDI_ScreenHeight Camera_Height
#define ReSTIRDI_FrameIndex Camera_FrameIndex
#define ReSTIRDI_CameraPosition Camera_Position

#endif
//Modify End
