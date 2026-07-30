//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <DX12Library/StructuredBuffer.h>

#include <Framework/Light.h>
#include <Framework/SharedUploadBuffer.h>

#include <Scene/SceneLighting.h>
#include <Framework/UnitySceneParser.h>

#include <DirectXMath.h>

#include <cstddef>
#include <memory>
#include <vector>

class CommandList;
class ComputeShader;
class RayTracingBindingSet;

class SceneLightManager final
{
public:
    SceneLightManager();

    void CreateDemoLights();
    void CreateFromUnityScene(const UnitySceneData& scene);
    void InitializeGpuBuffers(CommandList& commandList);
//Modify Begin:2026-07-30 by BestHui
    bool Upload(CommandList& commandList);
//Modify End
    void UpdateDynamicLights(float timeSeconds);
    bool DrawImGui();
    void BindComputeResources(CommandList& commandList, ComputeShader& shader);
    void BindRayTracingResources(RayTracingBindingSet& bindingSet);

    void FillCameraConstants(
        uint32_t& directionalLightCount,
        uint32_t& pointLightCount,
        uint32_t& areaLightCount,
        SkyLightData& skyLight) const;

    bool IsPointLightAnimationEnabled() const { return m_AnimatePointLights; }
    size_t GetPointLightCount() const { return m_PointLights.size(); }
    const std::vector<PointLight>& GetPointLights() const { return m_PointLights; }
    const std::vector<AreaLightData>& GetAreaLights() const { return m_AreaLights; }

private:
    void AddPointLightAtOrigin();
    void AddRandomPointLightInUpperHemisphere();
    void BuildGpuData();
    void UpdatePointLightGpuData(size_t lightIndex);
    void MarkDirectionalLightsDirty();
    void MarkDirectionalLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkPointLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkAreaLightsDirty();
    void MarkAreaLightsDirty(size_t beginIndex, size_t endIndex);

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

    DirectX::XMFLOAT3 m_NewPointLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewPointLightIntensity = 18.0f;
    float m_NewPointLightRange = 24.0f;
    float m_RandomPointLightSpawnRadius = 28.0f;
    bool m_AnimatePointLights = false;
};
//Modify End
