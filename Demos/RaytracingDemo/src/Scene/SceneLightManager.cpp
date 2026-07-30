//Modify Begin:2026-07-27 by BestHui
#include <Scene/SceneLightManager.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <DirectXMath.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
//Modify Begin:2026-07-30 by BestHui
    constexpr size_t InitialLightBufferCapacity = 1;
//Modify End

    size_t GrowLightBufferCapacity(const size_t currentCapacity, const size_t requiredCapacity)
    {
//Modify Begin:2026-07-30 by BestHui
        size_t newCapacity = std::max<size_t>(InitialLightBufferCapacity, currentCapacity);
//Modify End
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

//Modify Begin:2026-07-30 by BestHui
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
//Modify End
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
    m_SkyLightColor = { m_SkyLight.ColorAndIntensity.x, m_SkyLight.ColorAndIntensity.y, m_SkyLight.ColorAndIntensity.z };
    m_SkyLightIntensity = m_SkyLight.ColorAndIntensity.w;

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

void SceneLightManager::CreateFromScene(const Scene& scene)
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

//Modify Begin:2026-07-30 by BestHui
    m_SkyLight.ColorAndIntensity = scene.GetSkybox().AmbientColorAndIntensity;
    if (scene.GetSkybox().Texture.IsValid() &&
        m_SkyLight.ColorAndIntensity.x <= 0.0f &&
        m_SkyLight.ColorAndIntensity.y <= 0.0f &&
        m_SkyLight.ColorAndIntensity.z <= 0.0f)
    {
        m_SkyLight.ColorAndIntensity = { 1.0f, 1.0f, 1.0f, std::max(0.001f, m_SkyLight.ColorAndIntensity.w) };
    }
    m_SkyLightColor = { m_SkyLight.ColorAndIntensity.x, m_SkyLight.ColorAndIntensity.y, m_SkyLight.ColorAndIntensity.z };
    m_SkyLightIntensity = m_SkyLight.ColorAndIntensity.w;
//Modify End

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

    BuildGpuData();
    MarkDirectionalLightsDirty();
    MarkPointLightsDirty(0, m_PointLightGpuData.size());
    MarkAreaLightsDirty();
}

void SceneLightManager::InitializeGpuBuffers(CommandList& commandList)
{
    m_UploadBuffer = std::make_unique<SharedUploadBuffer>();
//Modify Begin:2026-07-30 by BestHui
    m_DirectionalLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_DirectionalLightGpuData.size());
    m_PointLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_PointLightGpuData.size());
    m_AreaLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_AreaLightGpuData.size());
//Modify End
    commandList.CopyStructuredBuffer(m_DirectionalLightBuffer, CreateBufferCapacityData<DirectionalLightData>(m_DirectionalLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_PointLightBuffer, CreateBufferCapacityData<PointLightData>(m_PointLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_AreaLightBuffer, CreateBufferCapacityData<AreaLightData>(m_AreaLightBufferCapacity));
}

//Modify Begin:2026-07-30 by BestHui
bool SceneLightManager::Upload(CommandList& commandList)
//Modify End
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
//Modify Begin:2026-07-30 by BestHui
    return directionalLightsRecreated || pointLightsRecreated || areaLightsRecreated;
//Modify End
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

    if (ImGui::CollapsingHeader("Light Counts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Directional: %zu", m_DirectionalLights.size());
        ImGui::Text("Point: %zu", m_PointLights.size());
        ImGui::Text("Area: %zu", m_AreaLights.size());
    }

    if (ImGui::CollapsingHeader("Sky Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool skyChanged = false;
        skyChanged |= ImGui::ColorEdit3("Sky Color", &m_SkyLightColor.x);
        skyChanged |= ImGui::SliderFloat("Sky Intensity", &m_SkyLightIntensity, 0.0f, 5.0f, "%.3f");
        if (skyChanged)
        {
            m_SkyLight.ColorAndIntensity = {
                std::max(0.0f, m_SkyLightColor.x),
                std::max(0.0f, m_SkyLightColor.y),
                std::max(0.0f, m_SkyLightColor.z),
                std::max(0.0f, m_SkyLightIntensity)
            };
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Directional Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("Directional Light List", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < m_DirectionalLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const DirectionalLight& light = m_DirectionalLights[i];
                ImGui::Text(
                    "#%zu Dir(%.2f, %.2f, %.2f) Intensity %.2f",
                    i,
                    light.m_DirectionWs.x,
                    light.m_DirectionWs.y,
                    light.m_DirectionWs.z,
                    light.m_Color.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    RemoveDirectionalLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::DragFloat3("Direction", &m_NewDirectionalLightDirection.x, 0.01f, -1.0f, 1.0f, "%.3f");
        ImGui::ColorEdit3("Directional Color", &m_NewDirectionalLightColor.x);
        ImGui::SliderFloat("Directional Intensity", &m_NewDirectionalLightIntensity, 0.0f, 20.0f, "%.2f");
        ImGui::SliderFloat("Angular Radius", &m_NewDirectionalLightAngularRadius, 0.0f, 0.25f, "%.4f");
        if (ImGui::Button("Add Directional Light"))
        {
            AddDirectionalLight();
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Animate Point Lights", &m_AnimatePointLights))
        {
            changed = true;
        }
        if (!m_PointLights.empty())
        {
            ImGui::Text("PointLight[0].Y: %.2f", m_PointLights.front().PositionWs.y);
        }
        if (ImGui::TreeNodeEx("Point Light List", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < m_PointLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const PointLight& light = m_PointLights[i];
                ImGui::Text(
                    "#%zu Pos(%.2f, %.2f, %.2f) Range %.2f Intensity %.2f%s",
                    i,
                    light.PositionWs.x,
                    light.PositionWs.y,
                    light.PositionWs.z,
                    light.Range,
                    light.Color.w,
                    i < m_PointLightAnimated.size() && m_PointLightAnimated[i] != 0 ? " Animated" : "");
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    RemovePointLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::ColorEdit3("Point Color", &m_NewPointLightColor.x);
        ImGui::SliderFloat("Point Intensity", &m_NewPointLightIntensity, 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Point Range", &m_NewPointLightRange, 0.1f, 100.0f, "%.1f");
        ImGui::SliderFloat("Random Spawn Radius", &m_RandomPointLightSpawnRadius, 1.0f, 80.0f, "%.1f");
        if (ImGui::Button("Add Point Light At Origin"))
        {
            AddPointLightAtOrigin();
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Random Point Light"))
        {
            AddRandomPointLightInUpperHemisphere();
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Area Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::TreeNodeEx("Area Light List", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < m_AreaLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const AreaLightData& light = m_AreaLights[i];
                ImGui::Text(
                    "#%zu Pos(%.2f, %.2f, %.2f) Size(%.2f, %.2f) Intensity %.2f",
                    i,
                    light.PositionAndRange.x,
                    light.PositionAndRange.y,
                    light.PositionAndRange.z,
                    light.AxisUAndExtent.w * 2.0f,
                    light.AxisVAndExtent.w * 2.0f,
                    light.ColorAndIntensity.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    RemoveAreaLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::DragFloat3("Area Position", &m_NewAreaLightPosition.x, 0.1f, -100.0f, 100.0f, "%.2f");
        ImGui::DragFloat3("Area Normal", &m_NewAreaLightNormal.x, 0.01f, -1.0f, 1.0f, "%.3f");
        ImGui::DragFloat2("Area Size", &m_NewAreaLightSize.x, 0.1f, 0.1f, 50.0f, "%.2f");
        ImGui::ColorEdit3("Area Color", &m_NewAreaLightColor.x);
        ImGui::SliderFloat("Area Intensity", &m_NewAreaLightIntensity, 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Area Range", &m_NewAreaLightRange, 0.1f, 200.0f, "%.1f");
        if (ImGui::Button("Add Area Light"))
        {
            AddAreaLight();
            changed = true;
        }
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
//Modify Begin:2026-07-30 by BestHui
    if (bindingSet.HasBinding("DirectionalLights"))
    {
        bindingSet.SetBuffer("DirectionalLights", m_DirectionalLightBuffer);
    }
    if (bindingSet.HasBinding("PointLights"))
    {
        bindingSet.SetBuffer("PointLights", m_PointLightBuffer);
    }
    if (bindingSet.HasBinding("AreaLights"))
    {
        bindingSet.SetBuffer("AreaLights", m_AreaLightBuffer);
    }
//Modify End
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

void SceneLightManager::AddDirectionalLight()
{
    const XMFLOAT3 direction = NormalizeVector(m_NewDirectionalLightDirection);
    DirectionalLight light{};
    light.m_DirectionWs = {
        direction.x,
        direction.y,
        direction.z,
        std::max(0.0f, m_NewDirectionalLightAngularRadius)
    };
    light.m_Color = {
        std::max(0.0f, m_NewDirectionalLightColor.x),
        std::max(0.0f, m_NewDirectionalLightColor.y),
        std::max(0.0f, m_NewDirectionalLightColor.z),
        std::max(0.0f, m_NewDirectionalLightIntensity)
    };

    const size_t lightIndex = m_DirectionalLights.size();
    m_DirectionalLights.push_back(light);

    DirectionalLightData gpuLight{};
    gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
    gpuLight.ColorAndIntensity = light.m_Color;
    m_DirectionalLightGpuData.push_back(gpuLight);
    MarkDirectionalLightsDirty(lightIndex, lightIndex + 1);
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

void SceneLightManager::AddAreaLight()
{
    const XMFLOAT3 normal = NormalizeVector(m_NewAreaLightNormal);
    XMFLOAT3 axisU{};
    XMFLOAT3 axisV{};
    BuildAreaLightAxes(normal, axisU, axisV);
    AreaLightData areaLight{};
    areaLight.PositionAndRange = {
        m_NewAreaLightPosition.x,
        m_NewAreaLightPosition.y,
        m_NewAreaLightPosition.z,
        std::max(0.1f, m_NewAreaLightRange)
    };
    areaLight.NormalAndType = { normal.x, normal.y, normal.z, 0.0f };
    areaLight.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, m_NewAreaLightSize.x) * 0.5f };
    areaLight.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, m_NewAreaLightSize.y) * 0.5f };
    areaLight.ColorAndIntensity = {
        std::max(0.0f, m_NewAreaLightColor.x),
        std::max(0.0f, m_NewAreaLightColor.y),
        std::max(0.0f, m_NewAreaLightColor.z),
        std::max(0.0f, m_NewAreaLightIntensity)
    };

    const size_t lightIndex = m_AreaLights.size();
    m_AreaLights.push_back(areaLight);
    m_AreaLightGpuData.push_back(areaLight);
    MarkAreaLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::RemoveDirectionalLight(const size_t lightIndex)
{
    if (lightIndex >= m_DirectionalLights.size())
    {
        return;
    }

    EraseAt(m_DirectionalLights, lightIndex);
    EraseAt(m_DirectionalLightGpuData, lightIndex);
    MarkDirectionalLightsDirty(lightIndex, m_DirectionalLightGpuData.size());
}

void SceneLightManager::RemovePointLight(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size())
    {
        return;
    }

    EraseAt(m_PointLights, lightIndex);
    EraseAt(m_PointLightGpuData, lightIndex);
    EraseAt(m_PointLightBaseY, lightIndex);
    EraseAt(m_PointLightPhase, lightIndex);
    EraseAt(m_PointLightOrbitRadius, lightIndex);
    EraseAt(m_PointLightOrbitSpeed, lightIndex);
    EraseAt(m_PointLightOrbitCenter, lightIndex);
    EraseAt(m_PointLightAnimated, lightIndex);
    MarkPointLightsDirty(lightIndex, m_PointLightGpuData.size());
}

void SceneLightManager::RemoveAreaLight(const size_t lightIndex)
{
    if (lightIndex >= m_AreaLights.size())
    {
        return;
    }

    EraseAt(m_AreaLights, lightIndex);
    EraseAt(m_AreaLightGpuData, lightIndex);
    MarkAreaLightsDirty(lightIndex, m_AreaLightGpuData.size());
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
