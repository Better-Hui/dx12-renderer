#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
#include <DX12Library/StructuredBuffer.h>

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

#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;
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

    static constexpr uint32_t MinRayTracingDescriptorArrayCapacity = 1;

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
    struct MaterialData
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        uint32_t DiffuseTextureIndex = 0;
        uint32_t NormalTextureIndex = 0;
        uint32_t MetallicTextureIndex = 0;
        uint32_t RoughnessTextureIndex = 0;
        uint32_t AmbientOcclusionTextureIndex = 0;
        uint32_t HasDiffuseMap = 0;
        uint32_t HasNormalMap = 0;
        uint32_t HasMetallicMap = 0;
        uint32_t HasRoughnessMap = 0;
        uint32_t HasAmbientOcclusionMap = 0;
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

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

    struct SceneObject
    {
        DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
        std::shared_ptr<Model> Model;
        uint32_t MaterialIndex = 0;
    };

    enum class PathTracingBackend
    {
        InlineRayQuery = 0,
        ShaderTableDxr = 1,
    };

    struct RayTracingSceneResourceLayout
    {
        uint32_t TextureDescriptorCapacity = MinRayTracingDescriptorArrayCapacity;
        uint32_t GeometryDescriptorCapacity = MinRayTracingDescriptorArrayCapacity;

        bool operator!=(const RayTracingSceneResourceLayout& other) const
        {
            return TextureDescriptorCapacity != other.TextureDescriptorCapacity ||
                GeometryDescriptorCapacity != other.GeometryDescriptorCapacity;
        }
    };

    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const MaterialData& material);
    uint32_t AddPbrMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        uint32_t normalTextureIndex,
        uint32_t metallicTextureIndex,
        uint32_t roughnessTextureIndex,
        uint32_t ambientOcclusionTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f,
        bool hasDiffuseMap = true,
        bool hasNormalMap = false,
        bool hasMetallicMap = false,
        bool hasRoughnessMap = false,
        bool hasAmbientOcclusionMap = false);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f);
    void LoadDeferredLightingScene(CommandList& commandList);
    void AddRaytracingInstances();
    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
    void BindRayTracingShaderResources();
    void BindRayTracingShaderResources(RayTracingBindingSet& shader);
    CameraConstants BuildCameraConstants() const;
    PipelineConstants BuildPipelineConstants() const;
    void ResetAccumulation(bool resetDenoiserHistory = true);
    bool IsDenoiserEnabled() const { return m_Denoisers.IsEnabled(); }
    DenoiserController& GetDenoisers() { return m_Denoisers; }
    const DenoiserController& GetDenoisers() const { return m_Denoisers; }
    void OnImGui();

    Camera m_Camera;
    friend struct RaytracingDemoPassAccess;
    friend class RaytracingDemoPasses::Builder;
    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<RayTracingBindingSet> m_DirectRayTracingBindingSet;
    std::unique_ptr<RayTracingBindingSet> m_IndirectRayTracingBindingSet;
    std::unique_ptr<ComputeShader> m_InlineDirectLightingShader;
    std::unique_ptr<ComputeShader> m_InlineIndirectLightingShader;
    std::unique_ptr<ComputeShader> m_LightingCompositeShader;
    DenoiserController m_Denoisers;
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    StructuredBuffer m_MaterialBuffer;
    StructuredBuffer m_GeometryBuffer;
    SceneLightManager m_Lights;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_SkyboxMesh;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
    std::shared_ptr<Shader> m_GBufferShader;
    std::shared_ptr<Shader> m_SkyboxShader;
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    std::vector<SceneObject> m_SceneObjects;
    std::vector<MaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;
    RayTracingSceneResourceLayout m_RayTracingSceneResourceLayout;

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
