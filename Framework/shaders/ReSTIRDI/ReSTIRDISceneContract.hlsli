//Modify Begin:2026-08-06 by BestHui
#ifndef FRAMEWORK_RESTIR_DI_SCENE_CONTRACT_HLSLI
#define FRAMEWORK_RESTIR_DI_SCENE_CONTRACT_HLSLI

#if !defined(FRAMEWORK_RESTIR_DI_SCENE_ADAPTER) || \
    !defined(ReSTIRDI_Surface) || \
    !defined(ReSTIRDI_LightSample) || \
    !defined(ReSTIRDI_LoadSurface) || \
    !defined(ReSTIRDI_GetLightCount) || \
    !defined(ReSTIRDI_SampleLight) || \
    !defined(ReSTIRDI_TestVisibility) || \
    !defined(ReSTIRDI_Luminance) || \
    !defined(ReSTIRDI_Random01) || \
    !defined(ReSTIRDI_InitializeRandomState) || \
    !defined(ReSTIRDI_ScreenWidth) || \
    !defined(ReSTIRDI_ScreenHeight) || \
    !defined(ReSTIRDI_FrameIndex) || \
    !defined(ReSTIRDI_CameraPosition)
#error "ReSTIR DI stages require a scene adapter that provides surface, light, visibility, random, and camera contracts."
#endif

#endif
//Modify End
