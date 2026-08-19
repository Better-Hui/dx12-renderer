#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
//Modify Begin:2026-07-29 by Hui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End

#include <Denoising/DenoiserController.h>
//Modify Begin:2026-08-07 by Hui
#include <Automation/RuntimeAutomationController.h>
#include <Profiling/ProfilerDisplayController.h>
#include <Profiling/RenderGraphTimingHistory.h>
//Modify End
//Modify Begin:2026-07-30 by Hui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End
#include <Framework/UI/ImGuiImpl.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/Pipeline/IndirectDrawCommandSignature.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
//Modify End
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
//Modify Begin:2026-08-10 by Hui
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>
//Modify End
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <Framework/Rendering/Upscaling/FrameFeaturesRuntime.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Pipeline/Shader.h>
//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
//Modify End

#include <RenderGraph/RaytracingDemoRenderPipelineController.h>
#include <Scene/SceneLightManager.h>
#include <Scene/RaytracingDemoSceneRuntimeController.h>
#include <Scene/SceneLighting.h>
#include <Scene/SceneResources.h>
#include <UI/DemoLightEditor.h>
#include <PathTracing/PathTracingPipelineController.h>
//Modify Begin:2026-07-30 by Hui
#include <Passes/CudaBloomPass.h>
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

//Modify Begin:2026-08-03 by Hui
#include <deque>
//Modify End
//Modify Begin:2026-07-30 by Hui
#include <chrono>
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

//Modify Begin:2026-07-30 by Hui
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

    //Modify Begin:2026-07-30 by Hui
    RaytracingDemoPassResources CreatePassResources();
    RaytracingDemoPassConfig CreatePassConfig() const;
    //Modify End

protected:
    void OnUpdate(UpdateEventArgs& e) override;
    void OnRender(RenderEventArgs& e) override;
    void OnKeyPressed(KeyEventArgs& e) override;
    void OnKeyReleased(KeyEventArgs& e) override;
    void OnMouseMoved(MouseMotionEventArgs& e) override;
//Modify Begin:2026-07-30 by Hui
    void OnMouseButtonPressed(MouseButtonEventArgs& e) override;
//Modify End
//Modify Begin:2026-08-06 by Hui
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
//Modify Begin:2026-07-30 by Hui
        DirectX::XMFLOAT4 TypeAndParams = { 0.0f, 0.0f, 0.0f, 0.0f };
//Modify End
    };

    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
//Modify Begin:2026-07-30 by Hui
    void SetMaterialShadingModel(MaterialShadingModel shadingModel);
//Modify End
//Modify Begin:2026-08-11 by Hui
    void SetMaxBounces(int maxBounces);
//Modify End
//Modify Begin:2026-07-30 by Hui
    void PrewarmRuntimeShadowVariants();
//Modify End
    void BindRayTracingShaderResources();
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<ShaderBlob> LoadShaderVariant(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {});
//Modify End
    PipelineConstants BuildPipelineConstants() const;
//Modify Begin:2026-07-28 by Hui
    void RebuildRenderGraph();
    void EnsureRenderGraphTopology();
    void ResetProfilerDisplay();
    void SetProfilerDisplayRefreshIntervalSeconds(double refreshIntervalSeconds);
//Modify Begin:2026-07-30 by Hui
    void UpdateRenderGraphFrameState();
//Modify End
    void PresentDisplayOutput();
//Modify End
//Modify Begin:2026-08-10 by Hui
    void ResetAccumulation(bool resetDenoiserHistory = true, bool resetReSTIRHistory = true);
//Modify End
//Modify Begin:2026-08-07 by Hui
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled() && !(m_DLSS.IsEnabled() && m_DLSS.IsRayReconstructionEnabled()); }
//Modify End
    Camera& GetSceneCamera() { return m_Scene.GetRuntimeCamera(); }
    const Camera& GetSceneCamera() const { return m_Scene.GetRuntimeCamera(); }
    DenoiserController& GetDenoisers() { return m_Denoisers; }
    const DenoiserController& GetDenoisers() const { return m_Denoisers; }
//Modify Begin:2026-07-27 by Hui
    void DrawPostBloomOverlays(CommandList& cmd);
    void DrawLightBillboards(CommandList& cmd);
//Modify End
//Modify Begin:2026-07-30 by Hui
//Modify Begin:2026-08-03 by Hui
    void LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath);
//Modify End
//Modify Begin:2026-07-30 by Hui
    void ResetCameraToInitialSceneState();
//Modify Begin:2026-08-16 by Hui
    void LoadStartupConfiguration();
//Modify End
    void InitializeRuntimeAutomation();
    void UpdateRuntimeAutomation(double totalTime);
    void ApplyRuntimeAutomationAction(uint32_t action, uint32_t value);
    void ApplyRuntimeAutomationMatrixCase(uint32_t caseIndex);
//Modify End
    void SaveCurrentScene();
    void SaveCurrentCameraToUnityScene();
//Modify End
    void OnImGui();

    //Modify Begin:2026-08-18 by Hui
    RaytracingDemoRenderPipelineController m_RenderPipeline;
    //Modify End
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<RaytracingDemoFrameState> m_RenderGraphFrameState = std::make_shared<RaytracingDemoFrameState>();
//Modify End
    int m_DebugTextureTarget = 0;
//Modify Begin:2026-07-27 by Hui
    FrameworkDeviceContext m_FrameworkDeviceContext;
//Modify Begin:2026-08-07 by Hui
    DLSS m_DLSS;
    DLSSFrameGenerationInputs m_FrameGenerationInputs;
    std::shared_ptr<ComputeShader> m_DLSSRayReconstructionPrepareShader;
//Modify End
//Modify Begin:2026-07-30 by Hui
    ShaderVariantManager m_ShaderVariants;
//Modify End
    PathTracingPipelineController m_PathTracingPipelines;
//Modify Begin:2026-07-30 by Hui
    ReSTIRDIPass m_DirectLightingReSTIRDIPass;
//Modify End
//Modify Begin:2026-08-10 by Hui
    ReSTIRGIPass m_IndirectLightingReSTIRGIPass;
//Modify End
//Modify End
    DenoiserController m_Denoisers;
//Modify Begin:2026-07-27 by Hui
    CudaBloomPass m_CudaBloom;
//Modify End
//Modify Begin:2026-07-29 by Hui
    GpuTimestampProfiler m_GpuTimestampProfiler;
//Modify Begin:2026-08-03 by Hui
    GpuTimestampProfiler m_AsyncComputeGpuTimestampProfiler;
//Modify Begin:2026-08-18 by Hui
    GpuTimestampProfiler m_CopyGpuTimestampProfiler;
//Modify End
//Modify End
    DemoProfiling::ProfilerDisplayController m_ProfilerDisplay;
//Modify Begin:2026-08-07 by Hui
    DemoProfiling::RenderGraphTimingHistory m_RenderGraphTimingHistory;
//Modify End
//Modify Begin:2026-08-03 by Hui
    bool m_GpuTimingEnabled = false;
    bool m_RenderGraphTimingCaptureEnabled = false;
//Modify End
//Modify Begin:2026-08-11 by Hui
    bool m_ReSTIRGIStageTimingEnabled = false;
//Modify End
//Modify End
    RaytracingDemoSceneResources m_SceneResources;
//Modify Begin:2026-08-18 by Hui
    RaytracingDemoSceneRuntimeController m_SceneRuntime;
//Modify End
    SceneLightManager m_Lights;
    DemoLightEditor m_LightEditor;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
//Modify Begin:2026-07-28 by Hui
    std::shared_ptr<Mesh> m_DisplayBlitMesh;
//Modify End
    std::shared_ptr<Shader> m_GBufferShader;
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<Shader> m_GBufferMeshletIndirectShader;
    std::shared_ptr<ComputeShader> m_MeshletCullShader;
    std::unique_ptr<IndirectDrawCommandSignature> m_MeshletDrawCommandSignature;
//Modify Begin:2026-07-31 by Hui
    std::shared_ptr<MeshShader> m_GBufferTaskMeshShader;
//Modify End
//Modify End
//Modify Begin:2026-07-28 by Hui
    std::shared_ptr<Shader> m_DisplayCompositeShader;
//Modify End
//Modify Begin:2026-07-28 by Hui
    std::shared_ptr<ComputeShader> m_SkyboxComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxEquirectangularComputeShader;
//Modify Begin:2026-08-06 by Hui
    std::shared_ptr<ComputeShader> m_SkyboxCubemapStripComputeShader;
//Modify End
//Modify End
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    float m_DeltaTime = 0.0f;
    DirectX::XMMATRIX m_PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
//Modify Begin:2026-08-11 by Hui
    int m_MaxBounces = 3;
    bool m_AccumulationEnabled = false;
//Modify End
//Modify Begin:2026-07-30 by Hui
    MaterialShadingModel m_MaterialShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-08-05 by Hui
//Modify Begin:2026-08-11 by Hui
    RaytracingDemoLightingTechnique m_DirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRDI;
    RaytracingDemoLightingTechnique m_IndirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRGI;
//Modify End
    ReSTIRDI m_DirectLightingReSTIRDI;
    bool m_ReSTIRDIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-10 by Hui
    ReSTIRGI m_IndirectLightingReSTIRGI;
    bool m_ReSTIRGIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-03 by Hui
    bool m_AsyncComputeEnabled = false;
//Modify Begin:2026-07-30 by Hui
    bool m_DebugSerializeAsyncCompute = false;
//Modify Begin:2026-08-07 by Hui
    bool m_ParallelDirectCommandRecordingEnabled = true;
//Modify End
    int m_DebugLightingTextureTarget = 0;
//Modify End
//Modify Begin:2026-07-30 by Hui
    bool m_SoftShadowsEnabled = false;
//Modify End
//Modify Begin:2026-07-30 by Hui
    DirectX::XMFLOAT3 m_InitialSceneCameraTranslation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 m_InitialSceneCameraRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_InitialSceneCameraYaw = 0.0f;
    float m_InitialSceneCameraPitch = 0.0f;
    bool m_HasInitialSceneCameraState = false;
//Modify Begin:2026-08-06 by Hui
    bool m_LeftMouseDragSincePress = false;
    bool m_LastLeftClickWasClick = false;
    bool m_LeftMouseNativeDoubleClick = false;
    int m_LeftMousePressX = 0;
    int m_LeftMousePressY = 0;
//Modify End

//Modify Begin:2026-08-07 by Hui
    DemoAutomation::RuntimeAutomationController m_RuntimeAutomation;
//Modify End
//Modify End
//Modify Begin:2026-07-30 by Hui
    bool m_UseMeshletGBuffer = true;
    bool m_DebugMeshletClusters = false;
//Modify Begin:2026-07-31 by Hui
    bool m_UseTaskShaderMeshlets = true;
//Modify End
//Modify End
//Modify Begin:2026-07-29 by Hui
    bool m_SkyboxEnabled = false;
//Modify End
    bool m_HasPreviousViewProjection = false;
    float m_CameraFov = 45.0f;
    float m_CameraNearClipPlane = 0.1f;
    float m_CameraFarClipPlane = 1000.0f;
    float m_MouseRotateSpeed = 0.1f;
    float m_MousePanSpeed = 0.04f;
    float m_MouseDollySpeed = 0.04f;
    float m_MouseWheelDollySpeed = 0.5f;
    PathTracingBackend m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
    int m_Width = 1;
    int m_Height = 1;
//Modify Begin:2026-07-30 by Hui
    Scene m_Scene;
    bool m_HasSceneCamera = false;
    std::string m_CameraSaveStatus;
//Modify Begin:2026-08-16 by Hui
    std::string m_StartupConfigurationStatus;
//Modify End
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
