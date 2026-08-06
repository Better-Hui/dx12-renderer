#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
//Modify Begin:2026-07-29 by BestHui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End

#include <Denoising/DenoiserController.h>
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
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Pipeline/Shader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
//Modify End

#include <RenderGraph/RenderGraphRoot.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneLighting.h>
#include <Scene/SceneResources.h>
#include <PathTracing/PathTracingPipelineController.h>
#include <Passes/CudaBloomPass.h>
//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

//Modify Begin:2026-08-03 by BestHui
#include <deque>
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

    RaytracingDemo(const std::wstring& name, int width, int height, GraphicsSettings graphicsSettings);

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
    void OnMouseWheel(MouseWheelEventArgs& e) override;
    void OnResize(ResizeEventArgs& e) override;

private:
    using MaterialData = RaytracingDemoMaterialData;
    using SceneObject = RaytracingDemoSceneObject;
    using PipelineConstants = RaytracingDemoPipelineConstants;
    using ModelConstants = RaytracingDemoModelConstants;
    using GBufferMaterialConstants = RaytracingDemoGBufferMaterialConstants;
    using GBufferDebugConstants = RaytracingDemoGBufferDebugConstants;

//Modify Begin:2026-08-03 by BestHui
    struct RenderGraphTimingQueue
    {
        std::string QueueName;
        std::vector<GpuTimestampSample> Samples;
    };

    struct RenderGraphTimingFrame
    {
        uint64_t FrameNumber = 0;
        std::vector<RenderGraphTimingQueue> Queues;
    };
//Modify End

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
    void PresentDisplayOutput();
//Modify End
    void ResetAccumulation(bool resetDenoiserHistory = true, bool resetReSTIRDIHistory = true);
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled(); }
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
    void SaveCurrentScene();
    void SaveCurrentCameraToUnityScene();
//Modify End
//Modify Begin:2026-08-03 by BestHui
    void ClearRenderGraphTimingHistory();
    bool DumpRenderGraphTimingHistory();
    void RecordRenderGraphTimingSamples(
        uint64_t frameNumber,
        const char* queueName,
        const std::vector<GpuTimestampSample>& samples);
//Modify End
    void OnImGui();

    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
//Modify Begin:2026-07-28 by BestHui
    bool m_RenderGraphDenoiserEnabled = false;
    bool m_RenderGraphCudaBloomEnabled = false;
//Modify Begin:2026-08-03 by BestHui
    bool m_RenderGraphAsyncComputeEnabled = false;
    PathTracingBackend m_RenderGraphPathTracingBackend = PathTracingBackend::InlineRayQuery;
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
//Modify Begin:2026-07-30 by BestHui
    ShaderVariantManager m_ShaderVariants;
//Modify End
    PathTracingPipelineController m_PathTracingPipelines;
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
//Modify Begin:2026-08-03 by BestHui
    std::deque<RenderGraphTimingFrame> m_RenderGraphTimingHistory;
//Modify End
    double m_LastGpuTimingUiUpdateTime = 0.0;
//Modify Begin:2026-08-03 by BestHui
    bool m_GpuTimingEnabled = false;
    bool m_RenderGraphTimingCaptureEnabled = false;
    int m_RenderGraphTimingHistoryCapacity = 300;
    std::string m_RenderGraphTimingExportStatus;
//Modify End
//Modify Begin:2026-08-02 by BestHui
    double m_LastRenderGraphCpuMilliseconds = 0.0;
//Modify End
//Modify End
    RaytracingDemoSceneResources m_SceneResources;
    SceneLightManager m_Lights;
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
//Modify End
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    float m_DeltaTime = 0.0f;
    DirectX::XMMATRIX m_PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
    int m_MaxBounces = 1;
    bool m_AccumulationEnabled = true;
//Modify Begin:2026-08-05 by BestHui
    RaytracingDemoLightingTechnique m_DirectLightingTechnique = RaytracingDemoLightingTechnique::ReSTIRDI;
    RaytracingDemoLightingTechnique m_IndirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    ReSTIRDI m_DirectLightingReSTIRDI;
    bool m_ReSTIRDIHistoryValid = false;
//Modify End
//Modify Begin:2026-08-03 by BestHui
    bool m_AsyncComputeEnabled = false;
//Modify Begin:2026-07-30 by BestHui
    bool m_DebugSerializeAsyncCompute = false;
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
