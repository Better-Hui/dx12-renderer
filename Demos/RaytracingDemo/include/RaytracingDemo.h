#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
//Modify Begin:2026-08-19 by Hui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End

#include <Denoising/DenoiserController.h>
//Modify Begin:2026-08-19 by Hui
#include <Automation/RuntimeAutomationController.h>
#include <Profiling/ProfilerDisplayController.h>
#include <Profiling/RenderGraphTimingHistory.h>
//Modify End
//Modify Begin:2026-08-19 by Hui
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Diagnostics/DiagnosticsSession.h>
//Modify End
#include <Framework/UI/ImGuiImpl.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
//Modify End
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>
#include <Framework/Rendering/Lighting/ActivePixelListController.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>
//Modify End
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <Framework/Rendering/PostProcess/AutoExposure.h>
#include <Framework/Rendering/Upscaling/FrameFeaturesRuntime.h>
#include <Framework/Scene/Scene.h>
//Modify Begin:2026-08-23 by Hui
#include <Framework/Scene/SceneImporter.h>
//Modify End
#include <Framework/Rendering/Pipeline/Shader.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
//Modify End

#include <RenderGraph/RaytracingDemoRenderPipelineController.h>
#include <Scene/SceneLightManager.h>
#include <Scene/RaytracingDemoSceneRuntimeController.h>
#include <Scene/SceneLighting.h>
#include <Scene/SceneResources.h>
#include <UI/DemoLightEditor.h>
#include <PathTracing/PathTracingPipelineController.h>
//Modify Begin:2026-08-19 by Hui
#include <Passes/CudaBloomPass.h>
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

//Modify Begin:2026-08-19 by Hui
#include <deque>
//Modify End
//Modify Begin:2026-08-19 by Hui
#include <chrono>
//Modify End
//Modify Begin:2026-08-21 by Hui
#include <exception>
//Modify End
//Modify Begin:2026-08-23 by Hui
#include <future>
//Modify End
//Modify Begin:2026-08-20 by Hui
#include <optional>
//Modify End
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;
namespace RaytracingDemoPasses
{
    class Builder;
}

class RaytracingDemo final : public Game
{
public:
    using Base = Game;

//Modify Begin:2026-08-19 by Hui
    RaytracingDemo(
        Application& application,
        const std::wstring& name,
        int width,
        int height,
        GraphicsSettings graphicsSettings,
        FrameFeatureServices frameFeatureServices);
//Modify End

    bool LoadContent() override;
    void UnloadContent() override;

//Modify Begin:2026-08-19 by Hui
    RaytracingDemoPassResources CreatePassResources();
    RaytracingDemoPassConfig CreatePassConfig() const;
    //Modify End

protected:
    void OnUpdate(UpdateEventArgs& e) override;
    void OnRender(RenderEventArgs& e) override;
    void OnKeyPressed(KeyEventArgs& e) override;
    void OnKeyReleased(KeyEventArgs& e) override;
    void OnMouseMoved(MouseMotionEventArgs& e) override;
    void OnMouseButtonPressed(MouseButtonEventArgs& e) override;
//Modify Begin:2026-08-19 by Hui
    void OnMouseButtonReleased(MouseButtonEventArgs& e) override;
//Modify End
    void OnMouseWheel(MouseWheelEventArgs& e) override;
    void OnResize(ResizeEventArgs& e) override;

private:
    using MaterialData = RaytracingDemoMaterialData;
    using SceneObject = RaytracingDemoSceneObject;
    using PipelineConstants = RaytracingDemoPipelineConstants;
    using ModelConstants = RaytracingDemoModelConstants;
    using GBufferMaterialConstants = RaytracingDemoGBufferMaterialConstants;
    using GBufferDebugConstants = RaytracingDemoGBufferDebugConstants;

    struct LightBillboardConstants
    {
        DirectX::XMFLOAT4 PositionAndSize = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 ColorAndAlpha = { 1.0f, 1.0f, 1.0f, 0.45f };
        DirectX::XMFLOAT4 CameraRight = { 1.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 CameraUp = { 0.0f, 1.0f, 0.0f, 0.0f };
//Modify Begin:2026-08-19 by Hui
        DirectX::XMFLOAT4 TypeAndParams = { 0.0f, 0.0f, 0.0f, 0.0f };
//Modify End
    };

//Modify Begin:2026-08-23 by Hui
    enum class StartupLoadStage : uint8_t
    {
        Bootstrap,
        ImportScene,
        UploadScene,
        CreateGeometryPipelines,
        CreatePostProcessPipelines,
        CreateLightingPipeline,
        FinalizeRendering,
        WaitForGpu,
        Complete,
    };
//Modify End

    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
//Modify Begin:2026-08-19 by Hui
    void SetMaterialShadingModel(MaterialShadingModel shadingModel);
//Modify End
//Modify Begin:2026-08-19 by Hui
    void SetMaxBounces(int maxBounces);
//Modify End
//Modify Begin:2026-08-19 by Hui
    void PrewarmRuntimeShadowVariants();
//Modify End
    void BindRayTracingShaderResources();
//Modify Begin:2026-08-19 by Hui
    std::shared_ptr<ShaderBlob> LoadShaderVariant(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {});
//Modify End
    PipelineConstants BuildPipelineConstants() const;
//Modify Begin:2026-08-19 by Hui
    void RebuildRenderGraph();
    void EnsureRenderGraphTopology();
    void ResetProfilerDisplay();
    void SetProfilerDisplayRefreshIntervalSeconds(double refreshIntervalSeconds);
    void UpdateRenderGraphFrameState();
    void PresentDisplayOutput();
//Modify End
//Modify Begin:2026-08-19 by Hui
    void ResetAccumulation(bool resetDenoiserHistory = true, bool resetReSTIRHistory = true);
//Modify End
//Modify Begin:2026-08-19 by Hui
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled() && !(m_DLSS.IsEnabled() && m_DLSS.IsRayReconstructionEnabled()); }
//Modify End
    Camera& GetSceneCamera() { return m_Scene.GetRuntimeCamera(); }
    const Camera& GetSceneCamera() const { return m_Scene.GetRuntimeCamera(); }
    DenoiserController& GetDenoisers() { return m_Denoisers; }
    const DenoiserController& GetDenoisers() const { return m_Denoisers; }
//Modify Begin:2026-08-19 by Hui
    void DrawPostBloomOverlays(CommandList& cmd);
    void DrawLightBillboards(CommandList& cmd);
//Modify End
//Modify Begin:2026-08-19 by Hui
    void LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath);
//Modify Begin:2026-08-23 by Hui
    bool AdvanceStartupLoad();
    void RenderStartupLoadingScreen();
    void SetStartupLoadStage(StartupLoadStage stage, std::string status, float progress);
    bool IsStartupLoadComplete() const { return m_StartupLoadStage == StartupLoadStage::Complete; }
//Modify End
    void ResetCameraToInitialSceneState();
    void LoadStartupConfiguration();
    void InitializeDiagnostics();
    void RecordDiagnosticsFailure(std::string stage, const std::exception& exception);
    void InitializeRuntimeAutomation();
    void UpdateRuntimeAutomation(double totalTime);
    void ApplyRuntimeAutomationAction(uint32_t action, uint32_t value);
    void ApplyRuntimeAutomationMatrixCase(uint32_t caseIndex);
    void CapturePendingAutomationScreenshot();
    void SaveCurrentScene();
    void SaveCurrentCameraToUnityScene();
//Modify End
    void OnImGui();

//Modify Begin:2026-08-19 by Hui
    RaytracingDemoRenderPipelineController m_RenderPipeline;
    //Modify End
    std::shared_ptr<RaytracingDemoFrameState> m_RenderGraphFrameState = std::make_shared<RaytracingDemoFrameState>();
    int m_DebugTextureTarget = 0;
//Modify Begin:2026-08-19 by Hui
    FrameworkDeviceContext m_FrameworkDeviceContext;
    FrameworkDiagnostics::DiagnosticsSession m_Diagnostics;
    DLSS m_DLSS;
    DLSSFrameGenerationInputs m_FrameGenerationInputs;
    std::shared_ptr<ComputeShader> m_DLSSRayReconstructionPrepareShader;
    ShaderVariantManager m_ShaderVariants;
    PathTracingPipelineController m_PathTracingPipelines;
    ActivePixelListController m_ActivePixels;
    ReSTIRDIPass m_DirectLightingReSTIRDIPass;
    ReSTIRGIPass m_IndirectLightingReSTIRGIPass;
//Modify End
    DenoiserController m_Denoisers;
//Modify Begin:2026-08-19 by Hui
    CudaBloomPass m_CudaBloom;
    AutoExposure m_AutoExposure;
//Modify End
//Modify Begin:2026-08-19 by Hui
    GpuTimestampProfiler m_GpuTimestampProfiler;
    GpuTimestampProfiler m_AsyncComputeGpuTimestampProfiler;
    GpuTimestampProfiler m_CopyGpuTimestampProfiler;
    DemoProfiling::ProfilerDisplayController m_ProfilerDisplay;
    DemoProfiling::RenderGraphTimingHistory m_RenderGraphTimingHistory;
    bool m_GpuTimingEnabled = false;
    bool m_RenderGraphTimingCaptureEnabled = false;
    bool m_ReSTIRGIStageTimingEnabled = false;
//Modify End
    RaytracingDemoSceneResources m_SceneResources;
//Modify Begin:2026-08-19 by Hui
    RaytracingDemoSceneRuntimeController m_SceneRuntime;
//Modify End
    SceneLightManager m_Lights;
    DemoLightEditor m_LightEditor;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
//Modify Begin:2026-08-19 by Hui
    std::shared_ptr<Mesh> m_DisplayBlitMesh;
//Modify End
    std::shared_ptr<Shader> m_GBufferShader;
//Modify Begin:2026-08-19 by Hui
    std::shared_ptr<Shader> m_GBufferMeshletIndirectShader;
    std::shared_ptr<ComputeShader> m_MeshletCullShader;
    std::unique_ptr<IndirectCommandSignature> m_MeshletDrawCommandSignature;
    std::shared_ptr<MeshShader> m_GBufferTaskMeshShader;
//Modify End
//Modify Begin:2026-08-19 by Hui
    std::shared_ptr<Shader> m_DisplayCompositeShader;
//Modify End
//Modify Begin:2026-08-19 by Hui
    std::shared_ptr<ComputeShader> m_SkyboxComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxEquirectangularComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxCubemapStripComputeShader;
//Modify End
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    float m_DeltaTime = 0.0f;
    DirectX::XMMATRIX m_PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
//Modify Begin:2026-08-19 by Hui
    int m_MaxBounces = 3;
    bool m_AccumulationEnabled = false;
//Modify End
//Modify Begin:2026-08-19 by Hui
    MaterialShadingModel m_MaterialShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-08-19 by Hui
    RaytracingDemoLightingTechnique m_DirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRDI;
    RaytracingDemoLightingTechnique m_IndirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRGI;
    ReSTIRDI m_DirectLightingReSTIRDI;
    bool m_ReSTIRDIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-19 by Hui
    ReSTIRGI m_IndirectLightingReSTIRGI;
    bool m_ReSTIRGIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-20 by Hui
    bool m_AsyncComputeEnabled = false;
    bool m_DebugSerializeAsyncCompute = false;
    bool m_ParallelDirectCommandRecordingEnabled = true;
    int m_DebugLightingTextureTarget = 0;
    bool m_SoftShadowsEnabled = false;
    DirectX::XMFLOAT3 m_InitialSceneCameraTranslation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 m_InitialSceneCameraRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_InitialSceneCameraYaw = 0.0f;
    float m_InitialSceneCameraPitch = 0.0f;
    bool m_HasInitialSceneCameraState = false;
    bool m_LeftMouseDragSincePress = false;
    bool m_LastLeftClickWasClick = false;
    bool m_LeftMouseNativeDoubleClick = false;
    int m_LeftMousePressX = 0;
    int m_LeftMousePressY = 0;

    DemoAutomation::RuntimeAutomationController m_RuntimeAutomation;
    std::optional<uint32_t> m_PendingAutomationScreenshot;
    bool m_UseMeshletGBuffer = true;
    bool m_DebugMeshletClusters = false;
    bool m_UseTaskShaderMeshlets = true;
    bool m_SkyboxEnabled = false;
    bool m_HasPreviousViewProjection = false;
    float m_CameraFov = 45.0f;
    float m_CameraNearClipPlane = 0.1f;
    float m_CameraFarClipPlane = 1000.0f;
    float m_MouseRotateSpeed = 0.1f;
    float m_MousePanSpeed = 0.04f;
    float m_MouseDollySpeed = 0.04f;
    float m_MouseWheelDollySpeed = 0.5f;
    PathTracingBackend m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
    bool m_OpenDxrCompatibilityPopup = false;
    PathTracingDispatchMode m_PathTracingDispatchMode = PathTracingDispatchMode::FullResolution;
    int m_Width = 1;
    int m_Height = 1;
    Scene m_Scene;
    bool m_HasSceneCamera = false;
    std::string m_CameraSaveStatus;
    std::string m_StartupConfigurationStatus;

//Modify Begin:2026-08-23 by Hui
    StartupLoadStage m_StartupLoadStage = StartupLoadStage::Bootstrap;
    std::filesystem::path m_StartupScenePath;
    std::future<SceneImportResult> m_StartupSceneImport;
    uint64_t m_StartupGpuFenceValue = 0;
    float m_StartupLoadProgress = 0.0f;
    std::string m_StartupLoadStatus = "Preparing renderer";
    std::chrono::steady_clock::time_point m_StartupLoadStartTime;
//Modify End

    struct
    {
        float Forward = 0.0f;
        float Backward = 0.0f;
        float Left = 0.0f;
        float Right = 0.0f;
        float Up = 0.0f;
        float Down = 0.0f;
        float Pitch = 0.0f;
        float Yaw = 0.0f;
        bool Shift = false;
    } m_CameraController;
};
//Modify End
