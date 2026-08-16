#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
//Modify Begin:2026-07-29 by BestHui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End

#include <Denoising/DenoiserController.h>
//Modify Begin:2026-08-07 by BestHui
#include <Automation/RuntimeAutomationController.h>
#include <Profiling/RenderGraphTimingHistory.h>
//Modify End
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End
#include <Framework/UI/ImGuiImpl.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/IndirectDrawCommandSignature.h>
#include <Framework/Rendering/Pipeline/MeshShader.h>
//Modify End
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Lighting/ReSTIRDIPass.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
//Modify Begin:2026-08-10 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRGIPass.h>
//Modify End
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Pipeline/Shader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
//Modify End

#include <RenderGraph/RenderGraphRoot.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneLighting.h>
#include <Scene/SceneResources.h>
#include <UI/DemoLightEditor.h>
#include <PathTracing/PathTracingPipelineController.h>
#include <Passes/CudaBloomPass.h>
//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

//Modify Begin:2026-08-03 by BestHui
#include <deque>
//Modify End
//Modify Begin:2026-07-30 by BestHui
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

//Modify Begin:2026-07-30 by BestHui
    RaytracingDemo(Application& application, const std::wstring& name, int width, int height, GraphicsSettings graphicsSettings);
//Modify End

    bool LoadContent() override;
    void UnloadContent() override;

    //Modify Begin:2026-07-30 by BestHui
    RaytracingDemoPassResources CreatePassResources();
    RaytracingDemoPassConfig CreatePassConfig() const;
    //Modify End

protected:
    void OnUpdate(UpdateEventArgs& e) override;
    void OnRender(RenderEventArgs& e) override;
    void OnKeyPressed(KeyEventArgs& e) override;
    void OnKeyReleased(KeyEventArgs& e) override;
    void OnMouseMoved(MouseMotionEventArgs& e) override;
//Modify Begin:2026-07-30 by BestHui
    void OnMouseButtonPressed(MouseButtonEventArgs& e) override;
//Modify End
//Modify Begin:2026-08-06 by BestHui
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
//Modify Begin:2026-07-30 by BestHui
        DirectX::XMFLOAT4 TypeAndParams = { 0.0f, 0.0f, 0.0f, 0.0f };
//Modify End
    };

    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
//Modify Begin:2026-07-30 by BestHui
    void SetMaterialShadingModel(MaterialShadingModel shadingModel);
//Modify End
//Modify Begin:2026-08-11 by BestHui
    void SetMaxBounces(int maxBounces);
//Modify End
//Modify Begin:2026-07-30 by BestHui
    void PrewarmRuntimeShadowVariants();
//Modify End
    void BindRayTracingShaderResources();
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<ShaderBlob> LoadShaderVariant(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {});
//Modify End
    PipelineConstants BuildPipelineConstants() const;
//Modify Begin:2026-07-28 by BestHui
    void RebuildRenderGraph();
    void EnsureRenderGraphTopology();
//Modify Begin:2026-07-30 by BestHui
    void UpdateRenderGraphFrameState();
//Modify End
    void PresentDisplayOutput();
//Modify End
//Modify Begin:2026-08-10 by BestHui
    void ResetAccumulation(bool resetDenoiserHistory = true, bool resetReSTIRHistory = true);
//Modify End
//Modify Begin:2026-08-07 by BestHui
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled() && !(m_DLSS.IsEnabled() && m_DLSS.IsRayReconstructionEnabled()); }
//Modify End
    Camera& GetSceneCamera() { return m_Scene.GetRuntimeCamera(); }
    const Camera& GetSceneCamera() const { return m_Scene.GetRuntimeCamera(); }
    DenoiserController& GetDenoisers() { return m_Denoisers; }
    const DenoiserController& GetDenoisers() const { return m_Denoisers; }
//Modify Begin:2026-07-27 by BestHui
    void DrawPostBloomOverlays(CommandList& cmd);
    void DrawLightBillboards(CommandList& cmd);
//Modify End
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-03 by BestHui
    void LoadSceneContent(CommandList& commandList, const std::filesystem::path& scenePath);
//Modify End
//Modify Begin:2026-07-30 by BestHui
    void ApplyStressTestSpheresState();
//Modify End
//Modify Begin:2026-07-30 by BestHui
    void ResetCameraToInitialSceneState();
//Modify Begin:2026-08-16 by BestHui
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

    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<RaytracingDemoFrameState> m_RenderGraphFrameState = std::make_shared<RaytracingDemoFrameState>();
//Modify End
//Modify Begin:2026-07-28 by BestHui
    bool m_RenderGraphDenoiserEnabled = false;
    DenoiserController::Algorithm m_RenderGraphDenoiserAlgorithm = DenoiserController::Algorithm::Off;
    bool m_RenderGraphCudaBloomEnabled = false;
//Modify Begin:2026-08-07 by BestHui
    bool m_RenderGraphDLSSEnabled = false;
    bool m_RenderGraphRayReconstructionEnabled = false;
    bool m_RenderGraphFrameGenerationEnabled = false;
//Modify End
//Modify Begin:2026-08-03 by BestHui
    bool m_RenderGraphAsyncComputeEnabled = false;
    PathTracingBackend m_RenderGraphPathTracingBackend = PathTracingBackend::InlineRayQuery;
//Modify Begin:2026-08-06 by BestHui
    RaytracingDemoLightingTechnique m_RenderGraphDirectLightingTechnique = RaytracingDemoLightingTechnique::None;
//Modify End
//Modify Begin:2026-08-10 by BestHui
//Modify Begin:2026-08-11 by BestHui
    RaytracingDemoLightingTechnique m_RenderGraphIndirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRGI;
    bool m_RenderGraphIndirectLightingEnabled = true;
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
    int m_RenderGraphLightingDebugTextureTarget = 0;
//Modify End
//Modify End
//Modify End
//Modify Begin:2026-07-31 by BestHui
    bool m_RenderGraphMeshletGBufferEnabled = false;
    bool m_RenderGraphTaskMeshletEnabled = true;
    bool m_RenderGraphMeshletDebugEnabled = false;
    int m_DebugTextureTarget = 0;
    int m_RenderGraphDebugTextureTarget = 0;
//Modify End
//Modify Begin:2026-07-27 by BestHui
    FrameworkDeviceContext m_FrameworkDeviceContext;
//Modify Begin:2026-08-07 by BestHui
    DLSS m_DLSS;
    DLSSFrameGenerationInputs m_FrameGenerationInputs;
    std::shared_ptr<ComputeShader> m_DLSSRayReconstructionPrepareShader;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    ShaderVariantManager m_ShaderVariants;
//Modify End
    PathTracingPipelineController m_PathTracingPipelines;
//Modify Begin:2026-07-30 by BestHui
    ReSTIRDIPass m_DirectLightingReSTIRDIPass;
//Modify End
//Modify Begin:2026-08-10 by BestHui
    ReSTIRGIPass m_IndirectLightingReSTIRGIPass;
//Modify End
//Modify End
    DenoiserController m_Denoisers;
//Modify Begin:2026-07-27 by BestHui
    CudaBloomPass m_CudaBloom;
//Modify End
//Modify Begin:2026-07-29 by BestHui
    GpuTimestampProfiler m_GpuTimestampProfiler;
    std::vector<GpuTimestampSample> m_GpuTimestampSamples;
    std::vector<GpuTimestampSample> m_GpuTimestampDisplaySamples;
//Modify Begin:2026-08-03 by BestHui
    GpuTimestampProfiler m_AsyncComputeGpuTimestampProfiler;
    std::vector<GpuTimestampSample> m_AsyncComputeGpuTimestampSamples;
    std::vector<GpuTimestampSample> m_AsyncComputeGpuTimestampDisplaySamples;
//Modify End
//Modify Begin:2026-08-07 by BestHui
    DemoProfiling::RenderGraphTimingHistory m_RenderGraphTimingHistory;
//Modify End
    double m_LastGpuTimingUiUpdateTime = 0.0;
//Modify Begin:2026-08-03 by BestHui
    bool m_GpuTimingEnabled = false;
    bool m_RenderGraphTimingCaptureEnabled = false;
//Modify End
//Modify Begin:2026-08-11 by BestHui
    bool m_ReSTIRGIStageTimingEnabled = false;
//Modify End
//Modify Begin:2026-08-02 by BestHui
    double m_LastRenderGraphCpuMilliseconds = 0.0;
//Modify End
//Modify End
    RaytracingDemoSceneResources m_SceneResources;
    SceneLightManager m_Lights;
    DemoLightEditor m_LightEditor;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
//Modify Begin:2026-07-28 by BestHui
    std::shared_ptr<Mesh> m_DisplayBlitMesh;
//Modify End
    std::shared_ptr<Shader> m_GBufferShader;
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<Shader> m_GBufferMeshletIndirectShader;
    std::shared_ptr<ComputeShader> m_MeshletCullShader;
    std::unique_ptr<IndirectDrawCommandSignature> m_MeshletDrawCommandSignature;
//Modify Begin:2026-07-31 by BestHui
    std::shared_ptr<MeshShader> m_GBufferTaskMeshShader;
//Modify End
//Modify End
//Modify Begin:2026-07-28 by BestHui
    std::shared_ptr<Shader> m_DisplayCompositeShader;
//Modify End
//Modify Begin:2026-07-28 by BestHui
    std::shared_ptr<ComputeShader> m_SkyboxComputeShader;
    std::shared_ptr<ComputeShader> m_SkyboxEquirectangularComputeShader;
//Modify Begin:2026-08-06 by BestHui
    std::shared_ptr<ComputeShader> m_SkyboxCubemapStripComputeShader;
//Modify End
//Modify End
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    float m_DeltaTime = 0.0f;
    DirectX::XMMATRIX m_PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
//Modify Begin:2026-08-11 by BestHui
    int m_MaxBounces = 3;
    bool m_AccumulationEnabled = false;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    MaterialShadingModel m_MaterialShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-08-05 by BestHui
//Modify Begin:2026-08-11 by BestHui
    RaytracingDemoLightingTechnique m_DirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRDI;
    RaytracingDemoLightingTechnique m_IndirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRGI;
//Modify End
    ReSTIRDI m_DirectLightingReSTIRDI;
    bool m_ReSTIRDIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-10 by BestHui
    ReSTIRGI m_IndirectLightingReSTIRGI;
    bool m_ReSTIRGIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-03 by BestHui
    bool m_AsyncComputeEnabled = false;
//Modify Begin:2026-07-30 by BestHui
    bool m_DebugSerializeAsyncCompute = false;
//Modify Begin:2026-08-07 by BestHui
    bool m_ParallelDirectCommandRecordingEnabled = true;
//Modify End
    int m_DebugLightingTextureTarget = 0;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    bool m_SoftShadowsEnabled = false;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    bool m_StressTestSpheresEnabled = false;
    bool m_StressTestSpheresStateDirty = false;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    DirectX::XMFLOAT3 m_InitialSceneCameraTranslation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 m_InitialSceneCameraRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_InitialSceneCameraYaw = 0.0f;
    float m_InitialSceneCameraPitch = 0.0f;
    bool m_HasInitialSceneCameraState = false;
//Modify Begin:2026-08-06 by BestHui
    bool m_LeftMouseDragSincePress = false;
    bool m_LastLeftClickWasClick = false;
    bool m_LeftMouseNativeDoubleClick = false;
    int m_LeftMousePressX = 0;
    int m_LeftMousePressY = 0;
//Modify End

//Modify Begin:2026-08-07 by BestHui
    DemoAutomation::RuntimeAutomationController m_RuntimeAutomation;
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
    bool m_UseMeshletGBuffer = true;
    bool m_DebugMeshletClusters = false;
//Modify Begin:2026-07-31 by BestHui
    bool m_UseTaskShaderMeshlets = true;
//Modify End
//Modify End
//Modify Begin:2026-07-29 by BestHui
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
//Modify Begin:2026-07-30 by BestHui
    Scene m_Scene;
    bool m_HasSceneCamera = false;
    std::string m_CameraSaveStatus;
//Modify Begin:2026-08-16 by BestHui
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
