//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Framework/Scene/Light.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Lighting/LightingGpuResources.h>

#include <Scene/SceneLighting.h>

#include <DirectXMath.h>

#include <cstddef>
#include <vector>

class CommandList;
class CommandContext;
class ComputeShader;
class RayTracingBindingSet;

class SceneLightManager final
{
public:
    struct PointLightAnimation
    {
        float BaseY = 0.0f;
        float Phase = 0.0f;
        float OrbitRadius = 0.0f;
        float OrbitSpeed = 0.0f;
        DirectX::XMFLOAT3 OrbitCenter = {};
        bool Enabled = false;
    };

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
    void SetPointLightAnimationEnabled(bool enabled) { m_AnimatePointLights = enabled; }
    void ApplyToScene(Scene& scene) const;
    const SkyLightData& GetSkyLight() const { return m_SkyLight; }
    void SetSkyLight(const SkyLightData& skyLight);

    bool AreDirectionalLightsEnabled() const { return m_DirectionalLightsEnabled; }
    bool ArePointLightsEnabled() const { return m_PointLightsEnabled; }
    bool AreAreaLightsEnabled() const { return m_AreaLightsEnabled; }
    void SetLightGroupSettings(bool directionalLightsEnabled, bool pointLightsEnabled, bool areaLightsEnabled);

    //Modify Begin:2026-07-30 by BestHui
    const std::vector<DirectionalLight>& GetDirectionalLights() const { return m_DirectionalLights; }
//Modify End
    size_t GetPointLightCount() const { return m_PointLights.size(); }
    const std::vector<PointLight>& GetPointLights() const { return m_PointLights; }
    const std::vector<AreaLightData>& GetAreaLights() const { return m_AreaLights; }
    DirectionalLight& EditDirectionalLight(size_t lightIndex);
    PointLight& EditPointLight(size_t lightIndex);
    AreaLightData& EditAreaLight(size_t lightIndex);
    PointLightAnimation GetPointLightAnimation(size_t lightIndex) const;
    void SetPointLightAnimation(size_t lightIndex, const PointLightAnimation& animation);
    void CommitDirectionalLightEdit(size_t lightIndex);
    void CommitPointLightEdit(size_t lightIndex);
    void CommitAreaLightEdit(size_t lightIndex);
    void AddDirectionalLight(const DirectionalLight& light);
    void AddPointLight(const PointLight& light, const PointLightAnimation& animation = {});
    void AddAreaLight(const AreaLightData& light);
    void RemoveDirectionalLight(size_t lightIndex);
    void RemovePointLight(size_t lightIndex);
    void RemoveAreaLight(size_t lightIndex);
//Modify Begin:2026-08-06 by BestHui
    size_t GetEmissiveMeshSurfaceEmitterCount() const;
//Modify End

private:
    void RebuildGpuResources();

    LightingGpuResources m_GpuResources;
    SkyLightData m_SkyLight = {};
    std::vector<DirectionalLight> m_DirectionalLights;
    std::vector<PointLight> m_PointLights;
    std::vector<AreaLightData> m_AreaLights;
    SurfaceEmitterSceneData m_MeshSurfaceEmitterData;

    std::vector<float> m_PointLightBaseY;
    std::vector<float> m_PointLightPhase;
    std::vector<float> m_PointLightOrbitRadius;
    std::vector<float> m_PointLightOrbitSpeed;
    std::vector<DirectX::XMFLOAT3> m_PointLightOrbitCenter;
    std::vector<uint8_t> m_PointLightAnimated;

    bool m_DirectionalLightsEnabled = true;
    bool m_PointLightsEnabled = true;
    bool m_AreaLightsEnabled = true;
    bool m_AnimatePointLights = false;
};
//Modify End
