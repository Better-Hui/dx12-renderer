//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <DX12Library/StructuredBuffer.h>

#include <Framework/Scene/Light.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Pipeline/SharedUploadBuffer.h>

#include <Scene/SceneLighting.h>

#include <DirectXMath.h>

#include <cstddef>
#include <memory>
#include <vector>

class CommandList;
class CommandContext;
class ComputeShader;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End
class RayTracingBindingSet;

class SceneLightManager final
{
public:
    explicit SceneLightManager(FrameworkDeviceContext& deviceContext);

    void CreateDemoLights();
    void CreateFromScene(const Scene& scene);
    void InitializeGpuBuffers(CommandList& commandList);
//Modify Begin:2026-07-30 by BestHui
    bool Upload(CommandList& commandList, uint64_t frameIndex);
//Modify End
    void UpdateDynamicLights(float timeSeconds);
    bool DrawImGui();
    void BindComputeResources(CommandContext& commandContext, ComputeShader& shader);
//Modify Begin:2026-08-03 by BestHui
    void PrepareAsyncComputeResources(CommandList& commandList) const;
//Modify End
    void BindRayTracingResources(RayTracingBindingSet& bindingSet);

    void FillCameraConstants(
        uint32_t& directionalLightCount,
        uint32_t& pointLightCount,
        uint32_t& areaLightCount,
        SkyLightData& skyLight) const;

    bool IsPointLightAnimationEnabled() const { return m_AnimatePointLights; }
//Modify Begin:2026-07-30 by BestHui
    const std::vector<DirectionalLight>& GetDirectionalLights() const { return m_DirectionalLights; }
//Modify End
    size_t GetPointLightCount() const { return m_PointLights.size(); }
    const std::vector<PointLight>& GetPointLights() const { return m_PointLights; }
    const std::vector<AreaLightData>& GetAreaLights() const { return m_AreaLights; }

private:
    void AddDirectionalLight();
    void AddPointLightAtOrigin();
    void AddRandomPointLightInUpperHemisphere();
    void AddAreaLight();
    void RemoveDirectionalLight(size_t lightIndex);
    void RemovePointLight(size_t lightIndex);
    void RemoveAreaLight(size_t lightIndex);
    void BuildGpuData();
    void UpdatePointLightGpuData(size_t lightIndex);
    void MarkDirectionalLightsDirty();
    void MarkDirectionalLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkPointLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkAreaLightsDirty();
    void MarkAreaLightsDirty(size_t beginIndex, size_t endIndex);

    FrameworkDeviceContext& m_DeviceContext;
    SkyLightData m_SkyLight = {};
    std::vector<DirectionalLight> m_DirectionalLights;
    std::vector<PointLight> m_PointLights;
    std::vector<AreaLightData> m_AreaLights;
    std::vector<DirectionalLightData> m_DirectionalLightGpuData;
    std::vector<PointLightData> m_PointLightGpuData;
    std::vector<AreaLightData> m_AreaLightGpuData;

    StructuredBuffer m_DirectionalLightBuffer;
    StructuredBuffer m_PointLightBuffer;
    StructuredBuffer m_AreaLightBuffer;
    std::unique_ptr<SharedUploadBuffer> m_UploadBuffer;

    size_t m_DirectionalLightBufferCapacity = 0;
    size_t m_PointLightBufferCapacity = 0;
    size_t m_AreaLightBufferCapacity = 0;
    size_t m_DirectionalLightDirtyBegin = 0;
    size_t m_DirectionalLightDirtyEnd = 0;
    size_t m_PointLightDirtyBegin = 0;
    size_t m_PointLightDirtyEnd = 0;
    size_t m_AreaLightDirtyBegin = 0;
    size_t m_AreaLightDirtyEnd = 0;

    std::vector<float> m_PointLightBaseY;
    std::vector<float> m_PointLightPhase;
    std::vector<float> m_PointLightOrbitRadius;
    std::vector<float> m_PointLightOrbitSpeed;
    std::vector<DirectX::XMFLOAT3> m_PointLightOrbitCenter;
    std::vector<uint8_t> m_PointLightAnimated;

    DirectX::XMFLOAT3 m_SkyLightColor = { 1.0f, 1.0f, 1.0f };
    float m_SkyLightIntensity = 0.35f;

    DirectX::XMFLOAT3 m_NewDirectionalLightDirection = { -0.35f, 0.8f, -0.48f };
    DirectX::XMFLOAT3 m_NewDirectionalLightColor = { 1.0f, 0.95f, 0.82f };
    float m_NewDirectionalLightIntensity = 1.0f;
    float m_NewDirectionalLightAngularRadius = 0.0f;

    DirectX::XMFLOAT3 m_NewPointLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewPointLightIntensity = 18.0f;
    float m_NewPointLightRange = 24.0f;
//Modify Begin:2026-07-30 by BestHui
    float m_NewPointLightSourceRadius = 0.25f;
//Modify End
    float m_RandomPointLightSpawnRadius = 28.0f;

    DirectX::XMFLOAT3 m_NewAreaLightPosition = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 m_NewAreaLightNormal = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT2 m_NewAreaLightSize = { 2.0f, 2.0f };
    DirectX::XMFLOAT3 m_NewAreaLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewAreaLightIntensity = 8.0f;
    float m_NewAreaLightRange = 35.0f;
    bool m_AnimatePointLights = false;
};
//Modify End
