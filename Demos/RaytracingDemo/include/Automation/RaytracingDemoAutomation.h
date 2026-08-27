#pragma once

//Modify Begin:2026-08-26 by Hui
#include <Automation/RuntimeAutomationController.h>
#include <Passes/RaytracingDemoPassResources.h>
#include <PathTracing/PathTracingPipelineController.h>

#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/Upscaling/DLSS.h>

#include <cstdint>
#include <string>
#include <vector>

namespace RaytracingDemoAutomation
{
    enum class Action : uint32_t
    {
        SoftShadows,
        StressSpheres,
        MeshletGBuffer,
        MeshletTaskShader,
        PathTracingBackend,
        DirectLighting,
        IndirectLighting,
        AsyncCompute,
        ParallelDirectCommandRecording,
        Skybox,
        Accumulation,
        GpuTiming,
        TimingCapture,
        ReSTIRGIStageTiming,
        ReSTIRGITemporalResampling,
        ReSTIRGISpatialResampling,
        ReSTIRGITemporalJacobian,
        ReSTIRGISpatialRayTracedBiasCorrection,
        DumpTiming,
        DLSS,
        MaterialShading,
        ReSTIRDIConfig,
        MaxBounces,
        Wait,
        VerifyActiveRayTracedPixelCount,
        CopyQueueValidation,
        VerifyCopyQueueValidation,
        DynamicRayTracingUpdate,
        VerifyDynamicRayTracingUpdate,
        VerifyDynamicSkinnedMeshCapability,
        Denoiser,
        OIDNStaticSpp,
        VerifyOIDNResult,
        OIDNCameraMotion,
        VerifyOIDNInvalidated,
        CaptureScreenshot,
        MatrixCase,
    };

    enum class ScreenshotCapture : uint32_t
    {
        PathTracingDirect,
        PathTracingIndirect,
        ReSTIRDI,
        ReSTIRGI,
        ReSTIRDIAndGI,
    };

    struct MatrixCase
    {
        PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
        RaytracingDemoLightingTechnique DirectLighting = RaytracingDemoLightingTechnique::None;
        RaytracingDemoLightingTechnique IndirectLighting = RaytracingDemoLightingTechnique::None;
        bool AsyncCompute = false;
        bool ParallelDirectCommandRecording = true;
        bool UseMeshletGBuffer = false;
        bool UseTaskShaderMeshlets = true;
        bool SoftShadows = false;
        bool StressSpheres = false;
        bool Skybox = false;
        bool Accumulation = false;
        DLSSMode DlssMode = DLSSMode::Disabled;
        MaterialShadingModel ShadingModel = MaterialShadingModel::Pbr;
        int MaxBounces = 1;
        std::string Name;
    };

    const std::vector<MatrixCase>& GetMatrixCases();
    const char* GetActionControlName(Action action);
    DemoAutomation::TestSuites CreateTestSuites();
}
//Modify End
