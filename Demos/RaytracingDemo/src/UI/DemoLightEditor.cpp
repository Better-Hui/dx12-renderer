//Modify Begin:2026-08-26 by Hui
#include <UI/DemoLightEditor.h>

#include <Scene/SceneLightManager.h>

#include <DirectXMath.h>
#include <imgui.h>
#include <Framework/UI/NumericWidgets.h>

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

    PointLight CreatePointLight(
        const XMFLOAT3& position,
        const XMFLOAT3& color,
        const float intensity,
        const float range,
        const float sourceRadius)
    {
        PointLight light({ position.x, position.y, position.z, 1.0f }, std::max(0.1f, range));
        light.Color = {
            std::max(0.0f, color.x),
            std::max(0.0f, color.y),
            std::max(0.0f, color.z),
            std::max(0.0f, intensity)
        };
        light.SourceRadius = std::max(0.0f, sourceRadius);
        light.RecalculateAttenuationCoefficients();
        return light;
    }
}

bool DemoLightEditor::Draw(SceneLightManager& lightManager)
{
    bool changed = false;

    if (ImGui::CollapsingHeader("Light Counts"))
    {
        ImGui::Text("Directional: %zu", lightManager.GetDirectionalLights().size());
        ImGui::Text("Point: %zu", lightManager.GetPointLights().size());
        ImGui::Text("Spot: %zu", lightManager.GetSpotLights().size());
        ImGui::Text("Area: %zu", lightManager.GetAreaLights().size());
        ImGui::Text("Mesh Surface Emitters: %zu", lightManager.GetEmissiveMeshSurfaceEmitterCount());
    }

    if (ImGui::CollapsingHeader("Sky Light"))
    {
        SkyLightData skyLight = lightManager.GetSkyLight();
        XMFLOAT3 color = {
            skyLight.ColorAndIntensity.x,
            skyLight.ColorAndIntensity.y,
            skyLight.ColorAndIntensity.z
        };
        float intensity = skyLight.ColorAndIntensity.w;
        const bool skyChanged =
            FrameworkImGui::SliderFloat3("Sky Color", &color.x, 0.0f, 10.0f, "%.3f") |
            FrameworkImGui::SliderFloat("Sky Intensity", &intensity, 0.0f, 100.0f, "%.3f");
        if (skyChanged)
        {
            skyLight.ColorAndIntensity = { color.x, color.y, color.z, intensity };
            lightManager.SetSkyLight(skyLight);
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Directional Lights"))
    {
        bool directionalLightsEnabled = lightManager.AreDirectionalLightsEnabled();
        if (ImGui::Checkbox("Enable Directional Lights", &directionalLightsEnabled))
        {
            lightManager.SetLightGroupSettings(
                directionalLightsEnabled,
                lightManager.ArePointLightsEnabled(),
                lightManager.AreAreaLightsEnabled());
            changed = true;
        }

        if (ImGui::CollapsingHeader("Directional Light List"))
        {
            const size_t directionalLightCount = lightManager.GetDirectionalLights().size();
            for (size_t index = 0; index < directionalLightCount; ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                DirectionalLight& light = lightManager.EditDirectionalLight(index);
                const bool open = ImGui::TreeNodeEx(
                    "DirectionalLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu Dir(%.2f, %.2f, %.2f) Intensity %.2f",
                    index,
                    light.m_DirectionWs.x,
                    light.m_DirectionWs.y,
                    light.m_DirectionWs.z,
                    light.m_Color.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    lightManager.RemoveDirectionalLight(index);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    const bool lightChanged =
                        FrameworkImGui::SliderFloat3("Direction", &light.m_DirectionWs.x, -1.0f, 1.0f, "%.4f") |
                        FrameworkImGui::SliderFloat3("Color", &light.m_Color.x, 0.0f, 10.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Intensity", &light.m_Color.w, 0.0f, 100.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Angular Radius", &light.m_DirectionWs.w, 0.0f, 0.1f, "%.5f");
                    if (lightChanged)
                    {
                        lightManager.CommitDirectionalLightEdit(index);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::PushID("NewDirectionalLight");
        if (ImGui::CollapsingHeader("New Directional Light"))
        {
            FrameworkImGui::SliderFloat3("Direction", &m_NewDirectionalLightDirection.x, -1.0f, 1.0f, "%.4f");
            FrameworkImGui::SliderFloat3("Color", &m_NewDirectionalLightColor.x, 0.0f, 10.0f, "%.3f");
            FrameworkImGui::SliderFloat("Intensity", &m_NewDirectionalLightIntensity, 0.0f, 100.0f, "%.3f");
            FrameworkImGui::SliderFloat("Angular Radius", &m_NewDirectionalLightAngularRadius, 0.0f, 0.1f, "%.5f");
            if (ImGui::Button("Add Directional Light"))
            {
                const XMFLOAT3 direction = NormalizeVector(m_NewDirectionalLightDirection);
                DirectionalLight light{};
                light.m_DirectionWs = { direction.x, direction.y, direction.z, m_NewDirectionalLightAngularRadius };
                light.m_Color = {
                    m_NewDirectionalLightColor.x,
                    m_NewDirectionalLightColor.y,
                    m_NewDirectionalLightColor.z,
                    m_NewDirectionalLightIntensity
                };
                lightManager.AddDirectionalLight(light);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Point Lights"))
    {
        bool pointLightsEnabled = lightManager.ArePointLightsEnabled();
        if (ImGui::Checkbox("Enable Point / Spot Lights", &pointLightsEnabled))
        {
            lightManager.SetLightGroupSettings(
                lightManager.AreDirectionalLightsEnabled(),
                pointLightsEnabled,
                lightManager.AreAreaLightsEnabled());
            changed = true;
        }

        bool animatePointLights = lightManager.IsPointLightAnimationEnabled();
        if (ImGui::Checkbox("Animate Point Lights", &animatePointLights))
        {
            lightManager.SetPointLightAnimationEnabled(animatePointLights);
            changed = true;
        }

        if (ImGui::CollapsingHeader("Point Light List"))
        {
            const size_t pointLightCount = lightManager.GetPointLights().size();
            for (size_t index = 0; index < pointLightCount; ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                PointLight& light = lightManager.EditPointLight(index);
                SceneLightManager::PointLightAnimation animation = lightManager.GetPointLightAnimation(index);
                const bool open = ImGui::TreeNodeEx(
                    "PointLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu %s I %.1f",
                    index,
                    animation.Enabled ? "Animated" : "Static",
                    light.Color.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    lightManager.RemovePointLight(index);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    XMFLOAT3 position = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z };
                    const bool positionChanged = FrameworkImGui::SliderFloat3("Position", &position.x, -500.0f, 500.0f, "%.3f");
                    bool pointChanged =
                        FrameworkImGui::SliderFloat3("Color", &light.Color.x, 0.0f, 10.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Intensity", &light.Color.w, 0.0f, 100.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Range", &light.Range, 0.1f, 500.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Source Radius", &light.SourceRadius, 0.0f, 10.0f, "%.4f");
                    if (ImGui::Checkbox("Animated", &animation.Enabled))
                    {
                        pointChanged = true;
                    }
                    if (positionChanged)
                    {
                        light.PositionWs = { position.x, position.y, position.z, 1.0f };
                        animation.BaseY = position.y;
                        animation.OrbitCenter = position;
                    }
                    if (pointChanged || positionChanged)
                    {
                        lightManager.SetPointLightAnimation(index, animation);
                        lightManager.CommitPointLightEdit(index);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::PushID("NewPointLight");
        if (ImGui::CollapsingHeader("New Point Light"))
        {
            FrameworkImGui::SliderFloat3("Color", &m_NewPointLightColor.x, 0.0f, 10.0f, "%.3f");
            FrameworkImGui::SliderFloat("Intensity", &m_NewPointLightIntensity, 0.0f, 100.0f, "%.3f");
            FrameworkImGui::SliderFloat("Range", &m_NewPointLightRange, 0.1f, 500.0f, "%.3f");
            FrameworkImGui::SliderFloat("Source Radius", &m_NewPointLightSourceRadius, 0.0f, 10.0f, "%.4f");
            FrameworkImGui::SliderFloat("Random Spawn Radius", &m_RandomPointLightSpawnRadius, 1.0f, 80.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Add At Origin"))
            {
                lightManager.AddPointLight(CreatePointLight(
                    { 0.0f, 0.0f, 0.0f },
                    m_NewPointLightColor,
                    m_NewPointLightIntensity,
                    m_NewPointLightRange,
                    m_NewPointLightSourceRadius));
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Random"))
            {
                static std::mt19937 randomGenerator{ std::random_device{}() };
                std::uniform_real_distribution<float> unitDistribution(0.0f, 1.0f);
                std::uniform_real_distribution<float> colorDistribution(0.25f, 1.0f);
                std::uniform_real_distribution<float> intensityDistribution(8.0f, 36.0f);
                std::uniform_real_distribution<float> rangeScaleDistribution(0.45f, 1.15f);
                const float spawnRadius = std::max(1.0f, m_RandomPointLightSpawnRadius);
                const float radius = spawnRadius * std::cbrt(unitDistribution(randomGenerator));
                const float cosTheta = unitDistribution(randomGenerator);
                const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                const float phase = XM_2PI * unitDistribution(randomGenerator);
                const XMFLOAT3 position = {
                    radius * sinTheta * std::cos(phase),
                    radius * cosTheta,
                    radius * sinTheta * std::sin(phase)
                };
                SceneLightManager::PointLightAnimation animation{};
                animation.BaseY = position.y;
                animation.Phase = phase;
                animation.OrbitRadius = std::max(1.0f, std::sqrt(position.x * position.x + position.z * position.z));
                animation.OrbitSpeed = 0.18f + unitDistribution(randomGenerator) * 0.55f;
                animation.Enabled = true;
                lightManager.AddPointLight(
                    CreatePointLight(
                        position,
                        { colorDistribution(randomGenerator), colorDistribution(randomGenerator), colorDistribution(randomGenerator) },
                        intensityDistribution(randomGenerator),
                        std::max(3.0f, m_NewPointLightRange * rangeScaleDistribution(randomGenerator)),
                        m_NewPointLightSourceRadius),
                    animation);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Spot Lights"))
    {
        ImGui::TextUnformatted("Spot lights use the Point / Spot light group toggle.");
        if (ImGui::CollapsingHeader("Spot Light List"))
        {
            const size_t spotLightCount = lightManager.GetSpotLights().size();
            for (size_t index = 0; index < spotLightCount; ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                SpotLight& light = lightManager.EditSpotLight(index);
                const bool open = ImGui::TreeNodeEx(
                    "SpotLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu Pos(%.2f, %.2f, %.2f) Cone %.1f / %.1f Intensity %.2f",
                    index,
                    light.PositionWs.x,
                    light.PositionWs.y,
                    light.PositionWs.z,
                    XMConvertToDegrees(light.InnerConeAngle),
                    XMConvertToDegrees(light.OuterConeAngle),
                    light.Intensity);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    lightManager.RemoveSpotLight(index);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    XMFLOAT3 position = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z };
                    XMFLOAT3 direction = { light.DirectionWs.x, light.DirectionWs.y, light.DirectionWs.z };
                    float innerAngleDegrees = XMConvertToDegrees(light.InnerConeAngle);
                    float outerAngleDegrees = XMConvertToDegrees(light.OuterConeAngle);
                    const bool lightChanged =
                        FrameworkImGui::SliderFloat3("Position", &position.x, -500.0f, 500.0f, "%.3f") |
                        FrameworkImGui::SliderFloat3("Direction", &direction.x, -1.0f, 1.0f, "%.4f") |
                        FrameworkImGui::SliderFloat3("Color", &light.Color.x, 0.0f, 10.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Intensity", &light.Intensity, 0.0f, 100.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Range", &light.Range, 0.1f, 500.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Inner Cone Angle", &innerAngleDegrees, 0.0f, 89.8f, "%.2f") |
                        FrameworkImGui::SliderFloat("Outer Cone Angle", &outerAngleDegrees, 0.1f, 89.9f, "%.2f");
                    if (lightChanged)
                    {
                        const XMFLOAT3 normalizedDirection = NormalizeVector(direction);
                        light.PositionWs = { position.x, position.y, position.z, 1.0f };
                        light.DirectionWs = { normalizedDirection.x, normalizedDirection.y, normalizedDirection.z, 0.0f };
                        light.InnerConeAngle = XMConvertToRadians((std::min)(innerAngleDegrees, outerAngleDegrees));
                        light.OuterConeAngle = XMConvertToRadians((std::max)(outerAngleDegrees, 0.1f));
                        lightManager.CommitSpotLightEdit(index);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::PushID("NewSpotLight");
        if (ImGui::CollapsingHeader("New Spot Light"))
        {
            FrameworkImGui::SliderFloat3("Position", &m_NewSpotLightPosition.x, -500.0f, 500.0f, "%.3f");
            FrameworkImGui::SliderFloat3("Direction", &m_NewSpotLightDirection.x, -1.0f, 1.0f, "%.4f");
            FrameworkImGui::SliderFloat3("Color", &m_NewSpotLightColor.x, 0.0f, 10.0f, "%.3f");
            FrameworkImGui::SliderFloat("Intensity", &m_NewSpotLightIntensity, 0.0f, 100.0f, "%.3f");
            FrameworkImGui::SliderFloat("Range", &m_NewSpotLightRange, 0.1f, 500.0f, "%.3f");
            FrameworkImGui::SliderFloat("Inner Cone Angle", &m_NewSpotLightInnerAngleDegrees, 0.0f, 89.8f, "%.2f");
            FrameworkImGui::SliderFloat("Outer Cone Angle", &m_NewSpotLightOuterAngleDegrees, 0.1f, 89.9f, "%.2f");
            if (ImGui::Button("Add Spot Light"))
            {
                const XMFLOAT3 direction = NormalizeVector(m_NewSpotLightDirection);
                SpotLight light{};
                light.PositionWs = { m_NewSpotLightPosition.x, m_NewSpotLightPosition.y, m_NewSpotLightPosition.z, 1.0f };
                light.DirectionWs = { direction.x, direction.y, direction.z, 0.0f };
                light.Color = { m_NewSpotLightColor.x, m_NewSpotLightColor.y, m_NewSpotLightColor.z, 1.0f };
                light.Intensity = m_NewSpotLightIntensity;
                light.Range = m_NewSpotLightRange;
                light.InnerConeAngle = XMConvertToRadians((std::min)(m_NewSpotLightInnerAngleDegrees, m_NewSpotLightOuterAngleDegrees));
                light.OuterConeAngle = XMConvertToRadians((std::max)(m_NewSpotLightOuterAngleDegrees, 0.1f));
                lightManager.AddSpotLight(light);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Area Lights"))
    {
        bool areaLightsEnabled = lightManager.AreAreaLightsEnabled();
        if (ImGui::Checkbox("Enable Area Lights", &areaLightsEnabled))
        {
            lightManager.SetLightGroupSettings(
                lightManager.AreDirectionalLightsEnabled(),
                lightManager.ArePointLightsEnabled(),
                areaLightsEnabled);
            changed = true;
        }

        if (ImGui::CollapsingHeader("Area Light List"))
        {
            const size_t areaLightCount = lightManager.GetAreaLights().size();
            for (size_t index = 0; index < areaLightCount; ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                AreaLightData& light = lightManager.EditAreaLight(index);
                const bool open = ImGui::TreeNodeEx(
                    "AreaLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu Pos(%.2f, %.2f, %.2f) Size(%.2f, %.2f) Intensity %.2f",
                    index,
                    light.PositionAndRange.x,
                    light.PositionAndRange.y,
                    light.PositionAndRange.z,
                    light.AxisUAndExtent.w * 2.0f,
                    light.AxisVAndExtent.w * 2.0f,
                    light.ColorAndIntensity.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    lightManager.RemoveAreaLight(index);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    XMFLOAT3 position = { light.PositionAndRange.x, light.PositionAndRange.y, light.PositionAndRange.z };
                    XMFLOAT3 normal = { light.NormalAndType.x, light.NormalAndType.y, light.NormalAndType.z };
                    XMFLOAT2 size = { light.AxisUAndExtent.w * 2.0f, light.AxisVAndExtent.w * 2.0f };
                    const bool lightChanged =
                        FrameworkImGui::SliderFloat3("Position", &position.x, -500.0f, 500.0f, "%.3f") |
                        FrameworkImGui::SliderFloat3("Normal", &normal.x, -1.0f, 1.0f, "%.4f") |
                        FrameworkImGui::SliderFloat2("Size", &size.x, 0.1f, 100.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Range", &light.PositionAndRange.w, 0.1f, 500.0f, "%.3f") |
                        FrameworkImGui::SliderFloat3("Color", &light.ColorAndIntensity.x, 0.0f, 10.0f, "%.3f") |
                        FrameworkImGui::SliderFloat("Intensity", &light.ColorAndIntensity.w, 0.0f, 100.0f, "%.3f");
                    if (lightChanged)
                    {
                        const XMFLOAT3 normalizedNormal = NormalizeVector(normal);
                        XMFLOAT3 axisU{};
                        XMFLOAT3 axisV{};
                        BuildAreaLightAxes(normalizedNormal, axisU, axisV);
                        light.PositionAndRange = { position.x, position.y, position.z, light.PositionAndRange.w };
                        light.NormalAndType = { normalizedNormal.x, normalizedNormal.y, normalizedNormal.z, light.NormalAndType.w };
                        light.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, size.x) * 0.5f };
                        light.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, size.y) * 0.5f };
                        lightManager.CommitAreaLightEdit(index);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        ImGui::PushID("NewAreaLight");
        if (ImGui::CollapsingHeader("New Area Light"))
        {
            FrameworkImGui::SliderFloat3("Position", &m_NewAreaLightPosition.x, -500.0f, 500.0f, "%.3f");
            FrameworkImGui::SliderFloat3("Normal", &m_NewAreaLightNormal.x, -1.0f, 1.0f, "%.4f");
            FrameworkImGui::SliderFloat2("Size", &m_NewAreaLightSize.x, 0.1f, 100.0f, "%.3f");
            FrameworkImGui::SliderFloat3("Color", &m_NewAreaLightColor.x, 0.0f, 10.0f, "%.3f");
            FrameworkImGui::SliderFloat("Intensity", &m_NewAreaLightIntensity, 0.0f, 100.0f, "%.3f");
            FrameworkImGui::SliderFloat("Range", &m_NewAreaLightRange, 0.1f, 500.0f, "%.3f");
            if (ImGui::Button("Add Area Light"))
            {
                const XMFLOAT3 normal = NormalizeVector(m_NewAreaLightNormal);
                XMFLOAT3 axisU{};
                XMFLOAT3 axisV{};
                BuildAreaLightAxes(normal, axisU, axisV);
                AreaLightData light{};
                light.PositionAndRange = { m_NewAreaLightPosition.x, m_NewAreaLightPosition.y, m_NewAreaLightPosition.z, m_NewAreaLightRange };
                light.NormalAndType = { normal.x, normal.y, normal.z, 0.0f };
                light.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, m_NewAreaLightSize.x) * 0.5f };
                light.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, m_NewAreaLightSize.y) * 0.5f };
                light.ColorAndIntensity = {
                    m_NewAreaLightColor.x,
                    m_NewAreaLightColor.y,
                    m_NewAreaLightColor.z,
                    m_NewAreaLightIntensity
                };
                lightManager.AddAreaLight(light);
                changed = true;
            }
        }
        ImGui::PopID();
    }

    return changed;
}
//Modify End
