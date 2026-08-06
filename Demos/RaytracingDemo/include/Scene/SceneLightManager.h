//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <DX12Library/StructuredBuffer.h>

#include <Framework/Scene/Light.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Lighting/SurfaceEmitter.h>
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
//Modify Begin:2026-08-06 by BestHui
    void SetEmissiveMeshSurfaceEmitters(SurfaceEmitterSceneData emitterData);
//Modify End
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
        uint32_t& surfaceEmitterCount,
        SkyLightData& skyLight) const;

    bool IsPointLightAnimationEnabled() const { return m_AnimatePointLights; }
    void ApplyToScene(Scene& scene) const;
//Modify Begin:2026-07-30 by BestHui
    const std::vector<DirectionalLight>& GetDirectionalLights() const { return m_DirectionalLights; }
//Modify End
    size_t GetPointLightCount() const { return m_PointLights.size(); }
    const std::vector<PointLight>& GetPointLights() const { return m_PointLights; }
    const std::vector<AreaLightData>& GetAreaLights() const { return m_AreaLights; }
//Modify Begin:2026-08-06 by BestHui
    size_t GetEmissiveMeshSurfaceEmitterCount() const { return m_MeshSurfaceEmitterData.Instances.size(); }
//Modify End

private:
    void AddDirectionalLight();
    void AddPointLightAtOrigin();
    void AddRandomPointLightInUpperHemisphere();
    void AddAreaLight();
    void RemoveDirectionalLight(size_t lightIndex);
    void RemovePointLight(size_t lightIndex);
    void RemoveAreaLight(size_t lightIndex);
    void BuildGpuData();
//Modify Begin:2026-08-06 by BestHui
    void RebuildSurfaceEmitterGpuData();
    void RebuildDirectLightSamplingCdf();
    void MarkDirectLightSamplingDirty();
    void UpdateAreaLightSurfaceEmitter(size_t lightIndex);
//Modify End
    void UpdatePointLightGpuData(size_t lightIndex);
    void MarkDirectionalLightsDirty();
    void MarkDirectionalLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkPointLightsDirty(size_t beginIndex, size_t endIndex);

    FrameworkDeviceContext& m_DeviceContext;
    SkyLightData m_SkyLight = {};
    std::vector<DirectionalLight> m_DirectionalLights;
    std::vector<PointLight> m_PointLights;
    std::vector<AreaLightData> m_AreaLights;
//Modify Begin:2026-08-06 by BestHui
    SurfaceEmitterSceneData m_MeshSurfaceEmitterData;
//Modify End
    std::vector<DirectionalLightData> m_DirectionalLightGpuData;
    std::vector<PointLightData> m_PointLightGpuData;
    std::vector<SurfaceEmitterGeometryData> m_SurfaceEmitterGeometryGpuData;
    std::vector<SurfaceEmitterTriangleData> m_SurfaceEmitterTriangleGpuData;
    std::vector<float> m_SurfaceEmitterTriangleCdfGpuData;
    std::vector<SurfaceEmitterInstanceData> m_SurfaceEmitterInstanceGpuData;

    StructuredBuffer m_DirectionalLightBuffer;
    StructuredBuffer m_PointLightBuffer;
    StructuredBuffer m_SurfaceEmitterGeometryBuffer;
    StructuredBuffer m_SurfaceEmitterTriangleBuffer;
    StructuredBuffer m_SurfaceEmitterTriangleCdfBuffer;
    StructuredBuffer m_SurfaceEmitterInstanceBuffer;
//Modify Begin:2026-08-06 by BestHui
    StructuredBuffer m_DirectLightCdfBuffer;
//Modify End
    std::unique_ptr<SharedUploadBuffer> m_UploadBuffer;

    size_t m_DirectionalLightBufferCapacity = 0;
    size_t m_PointLightBufferCapacity = 0;
    size_t m_SurfaceEmitterGeometryBufferCapacity = 0;
    size_t m_SurfaceEmitterTriangleBufferCapacity = 0;
    size_t m_SurfaceEmitterTriangleCdfBufferCapacity = 0;
    size_t m_SurfaceEmitterInstanceBufferCapacity = 0;
//Modify Begin:2026-08-06 by BestHui
    size_t m_DirectLightCdfBufferCapacity = 0;
//Modify End
    size_t m_DirectionalLightDirtyBegin = 0;
    size_t m_DirectionalLightDirtyEnd = 0;
    size_t m_PointLightDirtyBegin = 0;
    size_t m_PointLightDirtyEnd = 0;
    size_t m_SurfaceEmitterGeometryDirtyBegin = 0;
    size_t m_SurfaceEmitterGeometryDirtyEnd = 0;
    size_t m_SurfaceEmitterTriangleDirtyBegin = 0;
    size_t m_SurfaceEmitterTriangleDirtyEnd = 0;
    size_t m_SurfaceEmitterTriangleCdfDirtyBegin = 0;
    size_t m_SurfaceEmitterTriangleCdfDirtyEnd = 0;
    size_t m_SurfaceEmitterInstanceDirtyBegin = 0;
    size_t m_SurfaceEmitterInstanceDirtyEnd = 0;
//Modify Begin:2026-08-06 by BestHui
    size_t m_DirectLightCdfDirtyBegin = 0;
    size_t m_DirectLightCdfDirtyEnd = 0;
    std::vector<float> m_DirectLightCdfGpuData;
    uint32_t m_RectangleEmitterGeometryIndex = SurfaceEmitterInvalidMaterialIndex;
    size_t m_RectangleSurfaceEmitterOffset = 0;
//Modify End

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
    bool m_DirectionalLightsEnabled = true;
    bool m_PointLightsEnabled = true;
    bool m_AreaLightsEnabled = true;
    bool m_AnimatePointLights = false;
};
//Modify End
