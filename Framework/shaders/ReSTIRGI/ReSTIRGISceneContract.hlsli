#ifndef FRAMEWORK_RESTIR_GI_SCENE_CONTRACT_HLSLI
#define FRAMEWORK_RESTIR_GI_SCENE_CONTRACT_HLSLI

//Modify Begin:2026-08-10 by Hui
#if !defined(FRAMEWORK_RESTIR_GI_SCENE_ADAPTER) || \
    !defined(ReSTIRGI_Surface) || \
    !defined(ReSTIRGI_LoadSurface) || \
    !defined(ReSTIRGI_LoadMotionVector) || \
    !defined(ReSTIRGI_GenerateInitialSample) || \
    !defined(ReSTIRGI_EvaluateContribution) || \
    !defined(ReSTIRGI_TestVisibility) || \
    !defined(ReSTIRGI_TestVisibilityAt) || \
    !defined(ReSTIRGI_IndirectLighting) || \
    !defined(ReSTIRGI_Luminance) || \
    !defined(ReSTIRGI_Random01) || \
    !defined(ReSTIRGI_InitializeRandomState)
#error "ReSTIR GI stages require a scene adapter that provides surfaces, rays, evaluation, visibility, and random sampling."
#endif
//Modify End

#endif
