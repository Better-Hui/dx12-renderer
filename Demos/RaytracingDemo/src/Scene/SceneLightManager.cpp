//Modify Begin:2026-07-27 by BestHui
#include <Scene/SceneLightManager.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/ComputeShader.h>
#include <Framework/RayTracingShader.h>
#include <Framework/ShaderResourceView.h>

#include <DirectXMath.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <random>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    size_t GrowLightBufferCapacity(const size_t currentCapacity, const size_t requiredCapacity)
    {
        size_t newCapacity = std::max<size_t>(1, currentCapacity);
        while (newCapacity < requiredCapacity)
        {
            newCapacity *= 2;
        }
        return newCapacity;
    }

    void MarkDirtyRange(
        const size_t beginIndex,
        const size_t endIndex,
        size_t& dirtyBegin,
        size_t& dirtyEnd)
    {
        if (beginIndex >= endIndex)
        {
            return;
        }

        if (dirtyBegin == dirtyEnd)
        {
            dirtyBegin = beginIndex;
            dirtyEnd = endIndex;
            return;
        }

        dirtyBegin = std::min(dirtyBegin, beginIndex);
        dirtyEnd = std::max(dirtyEnd, endIndex);
    }

    template<typename T>
    std::vector<T> CreateBufferCapacityData(const size_t count)
    {
        return std::vector<T>(std::max<size_t>(1, count));
    }

    template<typename T>
    bool EnsureStructuredBufferCapacity(
        CommandList& commandList,
        StructuredBuffer& buffer,
        size_t& currentCapacity,
        const std::vector<T>& values)
    {
        if (values.size() <= currentCapacity)
        {
            return false;
        }

        if (buffer.GetD3D12Resource() != nullptr)
        {
            commandList.TrackResource(buffer);
        }

        currentCapacity = GrowLightBufferCapacity(currentCapacity, values.size());
        std::vector<T> capacityData(currentCapacity);
        std::copy(values.begin(), values.end(), capacityData.begin());
        commandList.CopyStructuredBuffer(buffer, capacityData);
        return true;
    }

    template<typename T>
    void UploadGpuLightRange(
        CommandList& commandList,
        SharedUploadBuffer& uploadBuffer,
        StructuredBuffer& destination,
        const std::vector<T>& values,
        const size_t beginIndex,
        const size_t endIndex)
    {
        const size_t clampedBegin = std::min(beginIndex, values.size());
        const size_t clampedEnd = std::min(endIndex, values.size());
        if (clampedBegin >= clampedEnd)
        {
            return;
        }

        const size_t elementCount = clampedEnd - clampedBegin;
        const uint64_t destinationOffset = static_cast<uint64_t>(clampedBegin * sizeof(T));
        uploadBuffer.Upload(commandList, destination, values.data() + clampedBegin, elementCount * sizeof(T), sizeof(T), destinationOffset);
    }
}

SceneLightManager::SceneLightManager()
    : m_DirectionalLightBuffer(L"Ray Tracing Directional Lights")
    , m_PointLightBuffer(L"Ray Tracing Point Lights")
    , m_AreaLightBuffer(L"Ray Tracing Area Lights")
{
}

void SceneLightManager::CreateDemoLights()
{
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_AreaLights.clear();
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

    AreaLightData areaLight{};
    areaLight.PositionAndRange = { -18.0f, 10.0f, 18.0f, 35.0f };
    areaLight.NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    areaLight.AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 4.0f };
    areaLight.AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 3.0f };
    areaLight.ColorAndIntensity = { 1.0f, 0.82f, 0.55f, 6.0f };
    m_AreaLights.push_back(areaLight);

    BuildGpuData();
    MarkDirectionalLightsDirty();
    MarkPointLightsDirty(0, m_PointLightGpuData.size());
    MarkAreaLightsDirty();
}

void SceneLightManager::InitializeGpuBuffers(CommandList& commandList)
{
    m_UploadBuffer = std::make_unique<SharedUploadBuffer>();
    m_DirectionalLightBufferCapacity = std::max<size_t>(1, m_DirectionalLightGpuData.size());
    m_PointLightBufferCapacity = std::max<size_t>(1, m_PointLightGpuData.size());
    m_AreaLightBufferCapacity = std::max<size_t>(1, m_AreaLightGpuData.size());
    commandList.CopyStructuredBuffer(m_DirectionalLightBuffer, CreateBufferCapacityData<DirectionalLightData>(m_DirectionalLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_PointLightBuffer, CreateBufferCapacityData<PointLightData>(m_PointLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_AreaLightBuffer, CreateBufferCapacityData<AreaLightData>(m_AreaLightBufferCapacity));
}

void SceneLightManager::Upload(CommandList& commandList)
{
    Assert(m_UploadBuffer != nullptr, "Light upload buffer is not initialized.");

    const bool directionalLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_DirectionalLightBuffer, m_DirectionalLightBufferCapacity, m_DirectionalLightGpuData);
    const bool pointLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_PointLightBuffer, m_PointLightBufferCapacity, m_PointLightGpuData);
    const bool areaLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_AreaLightBuffer, m_AreaLightBufferCapacity, m_AreaLightGpuData);

    if (directionalLightsRecreated)
    {
        m_DirectionalLightDirtyBegin = 0;
        m_DirectionalLightDirtyEnd = 0;
    }
    if (pointLightsRecreated)
    {
        m_PointLightDirtyBegin = 0;
        m_PointLightDirtyEnd = 0;
    }
    if (areaLightsRecreated)
    {
        m_AreaLightDirtyBegin = 0;
        m_AreaLightDirtyEnd = 0;
    }

    m_UploadBuffer->BeginFrame();
    if (m_DirectionalLightDirtyBegin < m_DirectionalLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_DirectionalLightBuffer, m_DirectionalLightGpuData, m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
        m_DirectionalLightDirtyBegin = 0;
        m_DirectionalLightDirtyEnd = 0;
    }

    if (m_PointLightDirtyBegin < m_PointLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_PointLightBuffer, m_PointLightGpuData, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
        m_PointLightDirtyBegin = 0;
        m_PointLightDirtyEnd = 0;
    }

    if (m_AreaLightDirtyBegin < m_AreaLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_AreaLightBuffer, m_AreaLightGpuData, m_AreaLightDirtyBegin, m_AreaLightDirtyEnd);
        m_AreaLightDirtyBegin = 0;
        m_AreaLightDirtyEnd = 0;
    }
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
        UpdatePointLightGpuData(i);
    }

    MarkPointLightsDirty(0, std::min(pointLightCount, m_PointLightGpuData.size()));
}

bool SceneLightManager::DrawImGui()
{
    bool changed = false;
    if (ImGui::Checkbox("Animate Point Lights", &m_AnimatePointLights))
    {
        changed = true;
    }
    ImGui::Text("Point Lights: %zu", m_PointLights.size());
    if (!m_PointLights.empty())
    {
        ImGui::Text("PointLight[0].Y: %.2f", m_PointLights.front().PositionWs.y);
    }
    ImGui::ColorEdit3("New Point Light Color", &m_NewPointLightColor.x);
    ImGui::SliderFloat("New Point Light Intensity", &m_NewPointLightIntensity, 0.0f, 100.0f, "%.1f");
    ImGui::SliderFloat("New Point Light Range", &m_NewPointLightRange, 0.1f, 100.0f, "%.1f");
    ImGui::SliderFloat("Random Light Spawn Radius", &m_RandomPointLightSpawnRadius, 1.0f, 80.0f, "%.1f");
    if (ImGui::Button("Add Point Light At Origin"))
    {
        AddPointLightAtOrigin();
        changed = true;
    }
    if (ImGui::Button("Add Random Point Light"))
    {
        AddRandomPointLightInUpperHemisphere();
        changed = true;
    }
    return changed;
}

void SceneLightManager::BindComputeResources(CommandList& commandList, ComputeShader& shader)
{
    if (shader.HasShaderResourceView("DirectionalLights"))
    {
        shader.SetShaderResourceView(commandList, "DirectionalLights", 0u, m_DirectionalLightBuffer);
    }

    if (shader.HasShaderResourceView("PointLights"))
    {
        shader.SetShaderResourceView(commandList, "PointLights", 0u, m_PointLightBuffer);
    }

    if (shader.HasShaderResourceView("AreaLights"))
    {
        shader.SetShaderResourceView(commandList, "AreaLights", 0u, m_AreaLightBuffer);
    }
}

void SceneLightManager::BindRayTracingResources(RayTracingBindingSet& bindingSet)
{
    bindingSet.SetBuffer("DirectionalLights", m_DirectionalLightBuffer);
    bindingSet.SetBuffer("PointLights", m_PointLightBuffer);
    bindingSet.SetBuffer("AreaLights", m_AreaLightBuffer);
}

void SceneLightManager::FillCameraConstants(
    uint32_t& directionalLightCount,
    uint32_t& pointLightCount,
    uint32_t& areaLightCount,
    SkyLightData& skyLight) const
{
    directionalLightCount = static_cast<uint32_t>(m_DirectionalLightGpuData.size());
    pointLightCount = static_cast<uint32_t>(m_PointLightGpuData.size());
    areaLightCount = static_cast<uint32_t>(m_AreaLightGpuData.size());
    skyLight = m_SkyLight;
}

void SceneLightManager::AddPointLightAtOrigin()
{
    PointLight light({ 0.0f, 0.0f, 0.0f, 1.0f }, std::max(0.1f, m_NewPointLightRange));
    light.Color = {
        std::max(0.0f, m_NewPointLightColor.x),
        std::max(0.0f, m_NewPointLightColor.y),
        std::max(0.0f, m_NewPointLightColor.z),
        std::max(0.0f, m_NewPointLightIntensity)
    };
    light.RecalculateAttenuationCoefficients();

    const size_t lightIndex = m_PointLights.size();
    m_PointLights.push_back(light);
    m_PointLightBaseY.push_back(0.0f);
    m_PointLightPhase.push_back(0.0f);
    m_PointLightOrbitRadius.push_back(0.0f);
    m_PointLightOrbitSpeed.push_back(0.0f);
    m_PointLightOrbitCenter.push_back({ 0.0f, 0.0f, 0.0f });
    m_PointLightAnimated.push_back(0);

    UpdatePointLightGpuData(lightIndex);
    MarkPointLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::AddRandomPointLightInUpperHemisphere()
{
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> unitDistribution(0.0f, 1.0f);
    std::uniform_real_distribution<float> colorDistribution(0.25f, 1.0f);
    std::uniform_real_distribution<float> intensityDistribution(8.0f, 36.0f);
    std::uniform_real_distribution<float> rangeScaleDistribution(0.45f, 1.15f);

    const float spawnRadius = std::max(1.0f, m_RandomPointLightSpawnRadius);
    const float radius = spawnRadius * std::cbrt(unitDistribution(rng));
    const float cosTheta = unitDistribution(rng);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = XM_2PI * unitDistribution(rng);
    const XMFLOAT3 position = {
        radius * sinTheta * std::cos(phi),
        radius * cosTheta,
        radius * sinTheta * std::sin(phi)
    };

    PointLight light({ position.x, position.y, position.z, 1.0f }, std::max(3.0f, m_NewPointLightRange * rangeScaleDistribution(rng)));
    light.Color = {
        colorDistribution(rng),
        colorDistribution(rng),
        colorDistribution(rng),
        intensityDistribution(rng)
    };
    light.RecalculateAttenuationCoefficients();

    const float orbitRadius = std::max(1.0f, std::sqrt(position.x * position.x + position.z * position.z));
    const size_t lightIndex = m_PointLights.size();
    m_PointLights.push_back(light);
    m_PointLightBaseY.push_back(position.y);
    m_PointLightPhase.push_back(phi);
    m_PointLightOrbitRadius.push_back(orbitRadius);
    m_PointLightOrbitSpeed.push_back(0.18f + unitDistribution(rng) * 0.55f);
    m_PointLightOrbitCenter.push_back({ 0.0f, 0.0f, 0.0f });
    m_PointLightAnimated.push_back(1);

    UpdatePointLightGpuData(lightIndex);
    MarkPointLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::BuildGpuData()
{
    m_DirectionalLightGpuData.clear();
    m_PointLightGpuData.clear();
    m_AreaLightGpuData.clear();

    m_DirectionalLightGpuData.reserve(m_DirectionalLights.size());
    for (const DirectionalLight& light : m_DirectionalLights)
    {
        DirectionalLightData gpuLight{};
        gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
        gpuLight.ColorAndIntensity = light.m_Color;
        m_DirectionalLightGpuData.push_back(gpuLight);
    }

    m_PointLightGpuData.reserve(m_PointLights.size());
    for (const PointLight& light : m_PointLights)
    {
        PointLightData gpuLight{};
        gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        gpuLight.ColorAndIntensity = light.Color;
        gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, 0.0f };
        m_PointLightGpuData.push_back(gpuLight);
    }

    m_AreaLightGpuData.reserve(m_AreaLights.size());
    for (const AreaLightData& light : m_AreaLights)
    {
        m_AreaLightGpuData.push_back(light);
    }
}

void SceneLightManager::UpdatePointLightGpuData(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size())
    {
        return;
    }

    if (m_PointLightGpuData.size() <= lightIndex)
    {
        m_PointLightGpuData.resize(lightIndex + 1);
    }

    const PointLight& light = m_PointLights[lightIndex];
    PointLightData& gpuLight = m_PointLightGpuData[lightIndex];
    gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
    gpuLight.ColorAndIntensity = light.Color;
    gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, 0.0f };
}

void SceneLightManager::MarkDirectionalLightsDirty()
{
    MarkDirectionalLightsDirty(0, m_DirectionalLightGpuData.size());
}

void SceneLightManager::MarkDirectionalLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    MarkDirtyRange(beginIndex, endIndex, m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
}

void SceneLightManager::MarkPointLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    MarkDirtyRange(beginIndex, endIndex, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
}

void SceneLightManager::MarkAreaLightsDirty()
{
    MarkAreaLightsDirty(0, m_AreaLightGpuData.size());
}

void SceneLightManager::MarkAreaLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    MarkDirtyRange(beginIndex, endIndex, m_AreaLightDirtyBegin, m_AreaLightDirtyEnd);
}
//Modify End
