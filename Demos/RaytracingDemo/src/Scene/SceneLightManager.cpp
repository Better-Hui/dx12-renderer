//Modify Begin:2026-08-26 by Hui
#include <Scene/SceneLightManager.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    XMFLOAT3 NormalizeVector(const XMFLOAT3& value)
    {
        const XMVECTOR vector = XMLoadFloat3(&value);
        if (XMVectorGetX(XMVector3LengthSq(vector)) <= 1.0e-8f)
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Normalize(vector));
        return result;
    }

    void BuildAreaLightAxes(const XMFLOAT3& normal, XMFLOAT3& axisU, XMFLOAT3& axisV)
    {
        const XMVECTOR normalVector = XMLoadFloat3(&normal);
        const XMVECTOR reference = std::abs(normal.y) < 0.99f ?
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) :
            XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const XMVECTOR axisUVector = XMVector3Normalize(XMVector3Cross(reference, normalVector));
        const XMVECTOR axisVVector = XMVector3Normalize(XMVector3Cross(normalVector, axisUVector));
        XMStoreFloat3(&axisU, axisUVector);
        XMStoreFloat3(&axisV, axisVVector);
    }

    template<typename T>
    void EraseAt(std::vector<T>& values, const size_t index)
    {
        if (index < values.size())
        {
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
}

SceneLightManager::SceneLightManager(FrameworkDeviceContext& deviceContext)
    : m_GpuResources(deviceContext)
{
}

void SceneLightManager::CreateDemoLights()
{
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_SpotLights.clear();
    m_AreaLights.clear();
    m_ImportedPointLightCount = 0;
    m_MeshSurfaceEmitterData = {};
    m_PointLightBaseY.clear();
    m_PointLightPhase.clear();
    m_PointLightOrbitRadius.clear();
    m_PointLightOrbitSpeed.clear();
    m_PointLightOrbitCenter.clear();
    m_PointLightAnimated.clear();

    m_SkyLight.ColorAndIntensity = { 0.85f, 0.9f, 1.0f, 0.35f };

    DirectionalLight sunLight{};
    sunLight.m_DirectionWs = { -0.35f, 0.8f, -0.48f, 0.0f };
    sunLight.m_Color = { 1.0f, 0.95f, 0.82f, 0.8f };
    m_DirectionalLights.push_back(sunLight);

    constexpr uint32_t DemoPointLightCount = 1;
    const XMFLOAT3 orbitCenter = { -12.0f, 6.0f, 18.0f };
    const XMFLOAT4 lightColors[DemoPointLightCount] = {
        { 1.0f, 0.35f, 0.28f, 20.0f },
    };

    m_PointLights.reserve(DemoPointLightCount);
    m_PointLightBaseY.reserve(DemoPointLightCount);
    m_PointLightPhase.reserve(DemoPointLightCount);
    m_PointLightOrbitRadius.reserve(DemoPointLightCount);
    m_PointLightOrbitSpeed.reserve(DemoPointLightCount);
    m_PointLightOrbitCenter.reserve(DemoPointLightCount);
    m_PointLightAnimated.reserve(DemoPointLightCount);

    for (uint32_t i = 0; i < DemoPointLightCount; ++i)
    {
        const float baseY = orbitCenter.y + static_cast<float>(i % 2) * 1.25f;
        const float phase = XM_2PI * static_cast<float>(i) / static_cast<float>(DemoPointLightCount);
        const float radius = 13.0f + static_cast<float>(i) * 2.0f;
        const float speed = 0.35f + static_cast<float>(i) * 0.07f;

        PointLight light(
            {
                orbitCenter.x + std::cos(phase) * radius,
                baseY,
                orbitCenter.z + std::sin(phase) * radius,
                1.0f
            },
            26.0f);
        light.Color = lightColors[i];
        light.RecalculateAttenuationCoefficients();

        m_PointLights.push_back(light);
        m_PointLightBaseY.push_back(baseY);
        m_PointLightPhase.push_back(phase);
        m_PointLightOrbitRadius.push_back(radius);
        m_PointLightOrbitSpeed.push_back(speed);
        m_PointLightOrbitCenter.push_back(orbitCenter);
        m_PointLightAnimated.push_back(1);
    }
    m_ImportedPointLightCount = m_PointLights.size();

    AreaLightData areaLight{};
    areaLight.PositionAndRange = { -18.0f, 10.0f, 18.0f, 35.0f };
    areaLight.NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    areaLight.AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 4.0f };
    areaLight.AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 3.0f };
    areaLight.ColorAndIntensity = { 1.0f, 0.82f, 0.55f, 6.0f };
    m_AreaLights.push_back(areaLight);

    RebuildGpuResources();
}

void SceneLightManager::CreateFromScene(const Scene& scene)
{
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_SpotLights.clear();
    m_AreaLights.clear();
    m_ImportedPointLightCount = 0;
    m_MeshSurfaceEmitterData = {};
    m_PointLightBaseY.clear();
    m_PointLightPhase.clear();
    m_PointLightOrbitRadius.clear();
    m_PointLightOrbitSpeed.clear();
    m_PointLightOrbitCenter.clear();
    m_PointLightAnimated.clear();

    const SceneLightGroupSettings& lightGroups = scene.GetLightGroupSettings();
    m_DirectionalLightsEnabled = lightGroups.DirectionalLightsEnabled;
    m_PointLightsEnabled = lightGroups.PointLightsEnabled;
    m_AreaLightsEnabled = lightGroups.AreaLightsEnabled;

    m_SkyLight.ColorAndIntensity = scene.GetSkybox().AmbientColorAndIntensity;
    if (scene.GetSkybox().Texture.IsValid() &&
        m_SkyLight.ColorAndIntensity.x <= 0.0f &&
        m_SkyLight.ColorAndIntensity.y <= 0.0f &&
        m_SkyLight.ColorAndIntensity.z <= 0.0f)
    {
        m_SkyLight.ColorAndIntensity = { 1.0f, 1.0f, 1.0f, std::max(0.001f, m_SkyLight.ColorAndIntensity.w) };
    }

    for (const DirectionalLight& light : scene.GetDirectionalLights())
    {
        m_DirectionalLights.push_back(light);
    }

    for (const PointLight& light : scene.GetPointLights())
    {
        m_PointLights.push_back(light);
        m_PointLightBaseY.push_back(light.PositionWs.y);
        m_PointLightPhase.push_back(0.0f);
        m_PointLightOrbitRadius.push_back(0.0f);
        m_PointLightOrbitSpeed.push_back(0.0f);
        m_PointLightOrbitCenter.push_back({ light.PositionWs.x, light.PositionWs.y, light.PositionWs.z });
        m_PointLightAnimated.push_back(0);
    }
    m_ImportedPointLightCount = m_PointLights.size();

    for (const SpotLight& spotLight : scene.GetSpotLights())
    {
        m_SpotLights.push_back(spotLight);
    }

    for (const AreaLight& light : scene.GetAreaLights())
    {
        AreaLightData areaLight{};
        areaLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        areaLight.NormalAndType = light.NormalWs;
        areaLight.AxisUAndExtent = light.AxisUWsAndExtent;
        areaLight.AxisVAndExtent = light.AxisVWsAndExtent;
        areaLight.ColorAndIntensity = light.Color;
        m_AreaLights.push_back(areaLight);
    }

    RebuildGpuResources();
}

void SceneLightManager::SetEmissiveMeshSurfaceEmitters(SurfaceEmitterSceneData emitterData)
{
    m_MeshSurfaceEmitterData = std::move(emitterData);
    m_GpuResources.SetMeshSurfaceEmitters(m_MeshSurfaceEmitterData);
}

void SceneLightManager::InitializeGpuBuffers(CommandList& commandList)
{
    m_GpuResources.Initialize(commandList);
}

bool SceneLightManager::Upload(CommandList& commandList, const uint64_t frameIndex)
{
    return m_GpuResources.Upload(commandList, frameIndex);
}

void SceneLightManager::UpdateDynamicLights(const float timeSeconds)
{
    const size_t pointLightCount = std::min(m_PointLights.size(), m_PointLightBaseY.size());
    for (size_t i = 0; i < pointLightCount; ++i)
    {
        if (i < m_PointLightAnimated.size() && m_PointLightAnimated[i] == 0)
        {
            continue;
        }

        const float radius = i < m_PointLightOrbitRadius.size() ? m_PointLightOrbitRadius[i] : 16.0f;
        const float speed = i < m_PointLightOrbitSpeed.size() ? m_PointLightOrbitSpeed[i] : 0.4f;
        const float phase = i < m_PointLightPhase.size() ? m_PointLightPhase[i] : 0.0f;
        const XMFLOAT3 orbitCenter = i < m_PointLightOrbitCenter.size() ? m_PointLightOrbitCenter[i] : XMFLOAT3{ -12.0f, 0.0f, 18.0f };
        const float angle = timeSeconds * speed + phase;
        m_PointLights[i].PositionWs.x = orbitCenter.x + std::cos(angle) * radius;
        m_PointLights[i].PositionWs.y = m_PointLightBaseY[i] + std::sin(angle * 1.7f) * 0.75f;
        m_PointLights[i].PositionWs.z = orbitCenter.z + std::sin(angle) * radius;
        m_GpuResources.UpdatePointLight(m_PointLights[i], i, false);
    }
}

void SceneLightManager::SetSkyLight(const SkyLightData& skyLight)
{
    m_SkyLight = skyLight;
    m_SkyLight.ColorAndIntensity.x = std::max(0.0f, m_SkyLight.ColorAndIntensity.x);
    m_SkyLight.ColorAndIntensity.y = std::max(0.0f, m_SkyLight.ColorAndIntensity.y);
    m_SkyLight.ColorAndIntensity.z = std::max(0.0f, m_SkyLight.ColorAndIntensity.z);
    m_SkyLight.ColorAndIntensity.w = std::max(0.0f, m_SkyLight.ColorAndIntensity.w);
}

void SceneLightManager::SetLightGroupSettings(
    const bool directionalLightsEnabled,
    const bool pointLightsEnabled,
    const bool areaLightsEnabled)
{
    m_DirectionalLightsEnabled = directionalLightsEnabled;
    m_PointLightsEnabled = pointLightsEnabled;
    m_AreaLightsEnabled = areaLightsEnabled;
    m_GpuResources.SetLightGroupSettings(
        m_DirectionalLightsEnabled,
        m_PointLightsEnabled,
        m_AreaLightsEnabled);
}

void SceneLightManager::ApplyToScene(Scene& scene) const
{
    std::vector<AreaLight> areaLights;
    areaLights.reserve(m_AreaLights.size());
    for (const AreaLightData& sourceLight : m_AreaLights)
    {
        AreaLight light{};
        light.PositionWs = sourceLight.PositionAndRange;
        light.NormalWs = sourceLight.NormalAndType;
        light.AxisUWsAndExtent = sourceLight.AxisUAndExtent;
        light.AxisVWsAndExtent = sourceLight.AxisVAndExtent;
        light.Color = sourceLight.ColorAndIntensity;
        light.Range = sourceLight.PositionAndRange.w;
        areaLights.push_back(light);
    }

    SceneSkybox skybox = scene.GetSkybox();
    skybox.AmbientColorAndIntensity = m_SkyLight.ColorAndIntensity;
    scene.SetSkybox(skybox);
    scene.SetLightGroupSettings({
        m_DirectionalLightsEnabled,
        m_PointLightsEnabled,
        m_AreaLightsEnabled
    });
    scene.SetDirectionalLights(m_DirectionalLights);
    std::vector<PointLight> importedPointLights;
    importedPointLights.reserve((std::min)(m_ImportedPointLightCount, m_PointLights.size()));
    importedPointLights.insert(
        importedPointLights.end(),
        m_PointLights.begin(),
        m_PointLights.begin() + static_cast<std::ptrdiff_t>(
            (std::min)(m_ImportedPointLightCount, m_PointLights.size())));
    scene.SetPointLights(std::move(importedPointLights));
    scene.SetSpotLights(m_SpotLights);
    scene.SetAreaLights(std::move(areaLights));
}

void SceneLightManager::BindComputeResources(CommandContext& commandContext, ComputeShader& shader)
{
    m_GpuResources.BindComputeResources(commandContext, shader);
}

void SceneLightManager::ForEachShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    m_GpuResources.ForEachShaderResource(action);
}

void SceneLightManager::BindRayTracingResources(RayTracingBindingSet& bindingSet)
{
    m_GpuResources.BindRayTracingResources(bindingSet);
}

void SceneLightManager::FillCameraConstants(
    uint32_t& directionalLightCount,
    uint32_t& pointLightCount,
    uint32_t& spotLightCount,
    uint32_t& surfaceEmitterCount,
    SkyLightData& skyLight) const
{
    directionalLightCount = m_GpuResources.GetDirectionalLightCount();
    pointLightCount = m_GpuResources.GetPointLightCount();
    spotLightCount = m_GpuResources.GetSpotLightCount();
    surfaceEmitterCount = m_GpuResources.GetSurfaceEmitterCount();
    skyLight = m_SkyLight;
}

DirectionalLight& SceneLightManager::EditDirectionalLight(const size_t lightIndex)
{
    Assert(lightIndex < m_DirectionalLights.size(), "Directional light index is invalid.");
    return m_DirectionalLights.at(lightIndex);
}

PointLight& SceneLightManager::EditPointLight(const size_t lightIndex)
{
    Assert(lightIndex < m_PointLights.size(), "Point light index is invalid.");
    return m_PointLights.at(lightIndex);
}

SpotLight& SceneLightManager::EditSpotLight(const size_t lightIndex)
{
    Assert(lightIndex < m_SpotLights.size(), "Spot light index is invalid.");
    return m_SpotLights.at(lightIndex);
}

AreaLightData& SceneLightManager::EditAreaLight(const size_t lightIndex)
{
    Assert(lightIndex < m_AreaLights.size(), "Area light index is invalid.");
    return m_AreaLights.at(lightIndex);
}

SceneLightManager::PointLightAnimation SceneLightManager::GetPointLightAnimation(const size_t lightIndex) const
{
    Assert(lightIndex < m_PointLights.size(), "Point light index is invalid.");
    return {
        m_PointLightBaseY.at(lightIndex),
        m_PointLightPhase.at(lightIndex),
        m_PointLightOrbitRadius.at(lightIndex),
        m_PointLightOrbitSpeed.at(lightIndex),
        m_PointLightOrbitCenter.at(lightIndex),
        m_PointLightAnimated.at(lightIndex) != 0
    };
}

void SceneLightManager::SetPointLightAnimation(const size_t lightIndex, const PointLightAnimation& animation)
{
    Assert(lightIndex < m_PointLights.size(), "Point light index is invalid.");
    m_PointLightBaseY.at(lightIndex) = animation.BaseY;
    m_PointLightPhase.at(lightIndex) = animation.Phase;
    m_PointLightOrbitRadius.at(lightIndex) = std::max(0.0f, animation.OrbitRadius);
    m_PointLightOrbitSpeed.at(lightIndex) = animation.OrbitSpeed;
    m_PointLightOrbitCenter.at(lightIndex) = animation.OrbitCenter;
    m_PointLightAnimated.at(lightIndex) = animation.Enabled ? 1u : 0u;
}

void SceneLightManager::CommitDirectionalLightEdit(const size_t lightIndex)
{
    DirectionalLight& light = EditDirectionalLight(lightIndex);
    const XMFLOAT3 direction = NormalizeVector({ light.m_DirectionWs.x, light.m_DirectionWs.y, light.m_DirectionWs.z });
    light.m_DirectionWs = { direction.x, direction.y, direction.z, std::max(0.0f, light.m_DirectionWs.w) };
    light.m_Color.x = std::max(0.0f, light.m_Color.x);
    light.m_Color.y = std::max(0.0f, light.m_Color.y);
    light.m_Color.z = std::max(0.0f, light.m_Color.z);
    light.m_Color.w = std::max(0.0f, light.m_Color.w);
    m_GpuResources.UpdateDirectionalLight(light, lightIndex);
}

void SceneLightManager::CommitPointLightEdit(const size_t lightIndex)
{
    PointLight& light = EditPointLight(lightIndex);
    light.PositionWs.w = 1.0f;
    light.Color.x = std::max(0.0f, light.Color.x);
    light.Color.y = std::max(0.0f, light.Color.y);
    light.Color.z = std::max(0.0f, light.Color.z);
    light.Color.w = std::max(0.0f, light.Color.w);
    light.Range = std::max(0.1f, light.Range);
    light.SourceRadius = std::max(0.0f, light.SourceRadius);
    light.RecalculateAttenuationCoefficients();
    m_GpuResources.UpdatePointLight(light, lightIndex);
}

void SceneLightManager::CommitSpotLightEdit(const size_t lightIndex)
{
    SpotLight& light = EditSpotLight(lightIndex);
    const XMFLOAT3 direction = NormalizeVector({ light.DirectionWs.x, light.DirectionWs.y, light.DirectionWs.z });
    const float outerConeAngle = std::clamp(light.OuterConeAngle, 0.001f, XM_PIDIV2 - 0.001f);
    const float innerConeAngle = std::clamp(light.InnerConeAngle, 0.0f, outerConeAngle);
    light.PositionWs.w = 1.0f;
    light.DirectionWs = { direction.x, direction.y, direction.z, 0.0f };
    light.Color.x = std::max(0.0f, light.Color.x);
    light.Color.y = std::max(0.0f, light.Color.y);
    light.Color.z = std::max(0.0f, light.Color.z);
    light.Intensity = std::max(0.0f, light.Intensity);
    light.InnerConeAngle = innerConeAngle;
    light.OuterConeAngle = outerConeAngle;
    light.Range = std::max(0.1f, light.Range);
    light.ConstantAttenuation = std::max(0.0f, light.ConstantAttenuation);
    light.LinearAttenuation = std::max(0.0f, light.LinearAttenuation);
    light.QuadraticAttenuation = std::max(0.0f, light.QuadraticAttenuation);
    light.SourceRadius = std::max(0.0f, light.SourceRadius);
    m_GpuResources.UpdateSpotLight(light, lightIndex);
}

void SceneLightManager::CommitAreaLightEdit(const size_t lightIndex)
{
    AreaLightData& light = EditAreaLight(lightIndex);
    const XMFLOAT3 normal = NormalizeVector({ light.NormalAndType.x, light.NormalAndType.y, light.NormalAndType.z });
    XMFLOAT3 axisU{};
    XMFLOAT3 axisV{};
    BuildAreaLightAxes(normal, axisU, axisV);
    light.PositionAndRange.w = std::max(0.1f, light.PositionAndRange.w);
    light.NormalAndType = { normal.x, normal.y, normal.z, light.NormalAndType.w };
    light.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, light.AxisUAndExtent.w) };
    light.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, light.AxisVAndExtent.w) };
    light.ColorAndIntensity.x = std::max(0.0f, light.ColorAndIntensity.x);
    light.ColorAndIntensity.y = std::max(0.0f, light.ColorAndIntensity.y);
    light.ColorAndIntensity.z = std::max(0.0f, light.ColorAndIntensity.z);
    light.ColorAndIntensity.w = std::max(0.0f, light.ColorAndIntensity.w);
    m_GpuResources.UpdateAreaLight(light, lightIndex);
}

void SceneLightManager::AddDirectionalLight(const DirectionalLight& light)
{
    m_DirectionalLights.push_back(light);
    CommitDirectionalLightEdit(m_DirectionalLights.size() - 1);
}

void SceneLightManager::AddPointLight(const PointLight& light, const PointLightAnimation& animation)
{
    m_PointLights.push_back(light);
    m_PointLightBaseY.push_back(animation.BaseY);
    m_PointLightPhase.push_back(animation.Phase);
    m_PointLightOrbitRadius.push_back(std::max(0.0f, animation.OrbitRadius));
    m_PointLightOrbitSpeed.push_back(animation.OrbitSpeed);
    m_PointLightOrbitCenter.push_back(animation.OrbitCenter);
    m_PointLightAnimated.push_back(animation.Enabled ? 1u : 0u);
    CommitPointLightEdit(m_PointLights.size() - 1);
}

void SceneLightManager::AddSpotLight(const SpotLight& light)
{
    m_SpotLights.push_back(light);
    CommitSpotLightEdit(m_SpotLights.size() - 1);
}

void SceneLightManager::AddAreaLight(const AreaLightData& light)
{
    m_AreaLights.push_back(light);
    CommitAreaLightEdit(m_AreaLights.size() - 1);
    RebuildGpuResources();
}

void SceneLightManager::RemoveDirectionalLight(const size_t lightIndex)
{
    if (lightIndex >= m_DirectionalLights.size())
    {
        return;
    }

    EraseAt(m_DirectionalLights, lightIndex);
    RebuildGpuResources();
}

void SceneLightManager::RemovePointLight(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size())
    {
        return;
    }

    EraseAt(m_PointLights, lightIndex);
    EraseAt(m_PointLightBaseY, lightIndex);
    EraseAt(m_PointLightPhase, lightIndex);
    EraseAt(m_PointLightOrbitRadius, lightIndex);
    EraseAt(m_PointLightOrbitSpeed, lightIndex);
    EraseAt(m_PointLightOrbitCenter, lightIndex);
    EraseAt(m_PointLightAnimated, lightIndex);
    RebuildGpuResources();
}

void SceneLightManager::RemoveSpotLight(const size_t lightIndex)
{
    if (lightIndex >= m_SpotLights.size())
    {
        return;
    }

    EraseAt(m_SpotLights, lightIndex);
    RebuildGpuResources();
}

void SceneLightManager::RemoveAreaLight(const size_t lightIndex)
{
    if (lightIndex >= m_AreaLights.size())
    {
        return;
    }

    EraseAt(m_AreaLights, lightIndex);
    RebuildGpuResources();
}

void SceneLightManager::RebuildGpuResources()
{
    m_GpuResources.Rebuild({
        m_DirectionalLights,
        m_PointLights,
        m_SpotLights,
        m_AreaLights,
        m_MeshSurfaceEmitterData,
        m_DirectionalLightsEnabled,
        m_PointLightsEnabled,
        m_AreaLightsEnabled
    });
}

size_t SceneLightManager::GetEmissiveMeshSurfaceEmitterCount() const
{
    return m_GpuResources.GetMeshSurfaceEmitterCount();
}

//Modify End
