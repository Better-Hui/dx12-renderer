#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>

#include <Denoising/DenoiserController.h>
#include <Framework/ImGuiImpl.h>
#include <Framework/Model.h>
#include <Framework/ComputeShader.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/RayTracingShader.h>
#include <Framework/Shader.h>

#include <RenderGraph/RenderGraphRoot.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneLighting.h>
#include <Scene/SceneResources.h>
#include <PathTracing/PathTracingPipelineController.h>
#include <PostProcessing/CudaBloomPass.h>

#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;
class RaytracingDemoRenderGraphBuilder;
struct RaytracingDemoPassAccess;

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

    struct CameraConstants
    {
        DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t MaxBounces = 5;
        uint32_t SamplesPerPixel = 1;
        uint32_t DirectionalLightCount = 0;
        uint32_t PointLightCount = 0;
        uint32_t AreaLightCount = 0;
        uint32_t FrameIndex = 0;
        uint32_t AccumulationFrameIndex = 0;
        uint32_t AccumulationEnabled = 1;
        uint32_t NRDDenoiserMode = 0;
        uint32_t Padding0 = 0;
        DirectX::XMFLOAT4 NRDReblurHitDistanceParameters = { 3.0f, 0.1f, 20.0f, 0.0f };
        uint32_t DirectLightingEnabled = 1;
        uint32_t IndirectLightingEnabled = 1;
        uint32_t Padding1 = 0;
        uint32_t Padding2 = 0;
        SkyLightData SkyLight = {};
    };

    struct PipelineConstants
    {
        DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT2 ScreenResolution = { 1.0f, 1.0f };
        DirectX::XMFLOAT2 ScreenTexelSize = { 1.0f, 1.0f };
    };

    struct ModelConstants
    {
        DirectX::XMMATRIX Model = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX ModelViewProjection = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseTransposeModel = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX PreviousModelViewProjection = DirectX::XMMatrixIdentity();
    };

    struct LightBillboardConstants
    {
        DirectX::XMFLOAT4 PositionAndSize = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 ColorAndAlpha = { 1.0f, 1.0f, 1.0f, 0.45f };
        DirectX::XMFLOAT4 CameraRight = { 1.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 CameraUp = { 0.0f, 1.0f, 0.0f, 0.0f };
    };

    struct GBufferMaterialConstants
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        uint32_t HasDiffuseMap = 0;
        uint32_t HasNormalMap = 0;
        uint32_t HasMetallicMap = 0;
        uint32_t HasRoughnessMap = 0;
        uint32_t HasAmbientOcclusionMap = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
        uint32_t Padding2 = 0;
    };

    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
    void BindRayTracingShaderResources();
    CameraConstants BuildCameraConstants() const;
    PipelineConstants BuildPipelineConstants() const;
//Modify Begin:2026-07-28 by BestHui
    void RebuildRenderGraph();
    void EnsureRenderGraphTopology();
    void PresentWithExternalPostProcess(const RenderGraph::RenderMetadata& metadata);
//Modify End
    void ResetAccumulation(bool resetDenoiserHistory = true);
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled(); }
    DenoiserController& GetDenoisers() { return m_Denoisers; }
    const DenoiserController& GetDenoisers() const { return m_Denoisers; }
//Modify Begin:2026-07-27 by BestHui
    void DrawPostBloomOverlays(CommandList& cmd);
    void DrawLightBillboards(CommandList& cmd);
//Modify End
    void OnImGui();

    Camera m_Camera;
    friend struct RaytracingDemoPassAccess;
    friend class RaytracingDemoPasses::Builder;
//Modify Begin:2026-07-28 by BestHui
    friend class RaytracingDemoRenderGraphBuilder;
//Modify End
    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
//Modify Begin:2026-07-28 by BestHui
    bool m_RenderGraphDenoiserEnabled = false;
//Modify End
//Modify Begin:2026-07-27 by BestHui
    PathTracingPipelineController m_PathTracingPipelines;
//Modify End
    DenoiserController m_Denoisers;
//Modify Begin:2026-07-27 by BestHui
    CudaBloomPass m_CudaBloom;
//Modify End
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    RaytracingDemoSceneResources m_SceneResources;
    SceneLightManager m_Lights;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_SkyboxMesh;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
    std::shared_ptr<Shader> m_GBufferShader;
    std::shared_ptr<Shader> m_SkyboxShader;
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    float m_DeltaTime = 0.0f;
    DirectX::XMMATRIX m_PreviousViewProjection = DirectX::XMMatrixIdentity();
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
    int m_MaxBounces = 1;
    bool m_AccumulationEnabled = true;
    bool m_DirectLightingEnabled = true;
    bool m_IndirectLightingEnabled = true;
    bool m_HasPreviousViewProjection = false;
    float m_CameraFov = 45.0f;
    float m_MouseRotateSpeed = 0.1f;
    float m_MousePanSpeed = 0.04f;
    float m_MouseDollySpeed = 0.04f;
    float m_MouseWheelDollySpeed = 0.5f;
    PathTracingBackend m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
    int m_Width = 1;
    int m_Height = 1;

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
