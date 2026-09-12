//Modify Begin:2026-09-10 by Hui
#include <Scene/RaytracingDemoSceneRuntimeController.h>

#include <Automation/RuntimeAutomationController.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneResources.h>

#include <DX12Library/Camera.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Scene/Scene.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace DirectX;

class RaytracingDemoSceneBehavior
{
public:
    virtual ~RaytracingDemoSceneBehavior() = default;

    virtual std::string_view GetName() const noexcept = 0;
    virtual void OnLoad(SceneLightManager& lights, const Camera& camera) = 0;
    virtual bool OnUpdate(
        SceneLightManager& lights,
        Camera& camera,
        float deltaTime,
        float totalTime,
        bool updateCamera,
        bool updateLights) = 0;
    virtual bool AdjustCameraAngularSpeed(float deltaRadiansPerSecond, float& newRadiansPerSecond) = 0;
    virtual bool SetCameraAngularSpeedToMaximum(float& newRadiansPerSecond) = 0;
    virtual bool AdjustCameraConeHalfAngle(float deltaRadians, float& newRadians) = 0;
    virtual void OnUnload(SceneLightManager& lights) = 0;
};

namespace
{
    constexpr size_t LowPolyStreetAnimatedAreaLightCount = 3;
    constexpr size_t LowPolyStreetAnimatedPointLightCount = 3;
    constexpr float LowPolyStreetCameraAngularSpeed = 1.0f;
    constexpr float LowPolyStreetCameraMinimumAngularSpeed = 0.05f;
    constexpr float LowPolyStreetCameraMaximumAngularSpeed = 1.0f;
    constexpr float LowPolyStreetCameraConeHalfAngle = XMConvertToRadians(15.0f);
    constexpr float LowPolyStreetCameraMinimumConeHalfAngle = XMConvertToRadians(2.0f);
    constexpr float LowPolyStreetCameraMaximumConeHalfAngle = XMConvertToRadians(30.0f);
    constexpr float LowPolyStreetCameraOrbitRampDuration = 2.0f;
    constexpr float LowPolyStreetLightAngularSpeed = 0.45f;
    constexpr float LowPolyStreetPointLightAngularOffset = XM_PI / 3.0f;
    constexpr float LowPolyStreetPointLightHeightOffset = 162.5f;
    constexpr float LowPolyStreetPointLightIntensity = 1200.0f;
    constexpr float LowPolyStreetPointLightSourceRadius = 8.75f;
    constexpr float LowPolyStreetPointLightRange = 100000.0f;

    std::string NormalizeSceneStem(const std::filesystem::path& scenePath)
    {
        std::string normalized;
        for (const unsigned char character : scenePath.stem().string())
        {
            if (std::isalnum(character) != 0)
            {
                normalized.push_back(static_cast<char>(std::tolower(character)));
            }
        }
        return normalized;
    }

    XMFLOAT3 RotateAroundY(
        const XMFLOAT3& value,
        const XMFLOAT3& center,
        const XMMATRIX& rotation)
    {
        const XMVECTOR relative = XMVectorSubtract(XMLoadFloat3(&value), XMLoadFloat3(&center));
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVectorAdd(XMVector3TransformNormal(relative, rotation), XMLoadFloat3(&center)));
        return result;
    }

    XMFLOAT3 RotateDirectionAroundY(const XMFLOAT4& value, const XMMATRIX& rotation)
    {
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3TransformNormal(XMLoadFloat4(&value), rotation));
        return result;
    }

    class LowPolyStreetSceneBehavior final : public RaytracingDemoSceneBehavior
    {
    public:
        std::string_view GetName() const noexcept override
        {
            return "LowPolyStreet.ShowreelOrbit";
        }

        void OnLoad(SceneLightManager& lights, const Camera& camera) override
        {
            const std::vector<AreaLightData>& areaLights = lights.GetAreaLights();
            if (areaLights.size() < LowPolyStreetAnimatedAreaLightCount)
            {
                throw std::runtime_error(
                    "LowPolyStreet scene behavior requires the three imported area lights.");
            }

            m_InitialLights.assign(
                areaLights.begin(),
                areaLights.begin() + static_cast<std::ptrdiff_t>(LowPolyStreetAnimatedAreaLightCount));
            m_OrbitCenter = {};
            for (const AreaLightData& light : m_InitialLights)
            {
                m_OrbitCenter.x += light.PositionAndRange.x;
                m_OrbitCenter.z += light.PositionAndRange.z;
            }
            m_OrbitCenter.x /= static_cast<float>(m_InitialLights.size());
            m_OrbitCenter.z /= static_cast<float>(m_InitialLights.size());

            XMStoreFloat3(&m_InitialCameraPosition, camera.GetTranslation());
            XMFLOAT3 initialForward{};
            XMStoreFloat3(
                &initialForward,
                XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camera.GetRotation()));
            const float horizontalForwardLengthSquared =
                initialForward.x * initialForward.x + initialForward.z * initialForward.z;
            const float targetDistance = horizontalForwardLengthSquared > 1.e-6f
                ? ((m_OrbitCenter.x - m_InitialCameraPosition.x) * initialForward.x +
                   (m_OrbitCenter.z - m_InitialCameraPosition.z) * initialForward.z) /
                    horizontalForwardLengthSquared
                : 0.0f;
            m_LookAtTarget = {
                m_OrbitCenter.x,
                m_InitialCameraPosition.y + initialForward.y * targetDistance,
                m_OrbitCenter.z
            };

            const XMVECTOR cameraPosition = XMLoadFloat3(&m_InitialCameraPosition);
            const XMVECTOR lookAtTarget = XMLoadFloat3(&m_LookAtTarget);
            const XMVECTOR cameraAxisVector = XMVectorSubtract(cameraPosition, lookAtTarget);
            const float cameraAxisLength = XMVectorGetX(XMVector3Length(cameraAxisVector));
            if (cameraAxisLength <= 1.e-4f)
            {
                throw std::runtime_error(
                    "LowPolyStreet scene behavior requires the camera to be separated from the house center.");
            }

            const XMVECTOR cameraAxis = XMVectorScale(cameraAxisVector, 1.0f / cameraAxisLength);
            XMVECTOR referenceUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            if (std::abs(XMVectorGetX(XMVector3Dot(cameraAxis, referenceUp))) > 0.98f)
            {
                referenceUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            }
            const XMVECTOR orbitBasisU = XMVector3Normalize(XMVector3Cross(referenceUp, cameraAxis));
            const XMVECTOR orbitBasisV = XMVector3Normalize(XMVector3Cross(cameraAxis, orbitBasisU));
            XMStoreFloat3(&m_CameraOrbitBasisU, orbitBasisU);
            XMStoreFloat3(&m_CameraOrbitBasisV, orbitBasisV);
            m_CameraAxisLength = cameraAxisLength;
            m_CameraOrbitRadius = m_CameraAxisLength * std::tan(m_CameraConeHalfAngle);

            float pointLightHeight = m_InitialLights.front().PositionAndRange.y;
            for (const AreaLightData& light : m_InitialLights)
            {
                pointLightHeight = (std::min)(pointLightHeight, light.PositionAndRange.y);
            }
            pointLightHeight -= LowPolyStreetPointLightHeightOffset;

            m_InitialPointLights.clear();
            m_InitialPointLights.reserve(LowPolyStreetAnimatedPointLightCount);
            m_PointLightOffset = (std::min)(
                lights.GetImportedPointLightCount(),
                lights.GetPointLightCount());
            const size_t persistedPointLightCount = lights.GetPointLightCount() - m_PointLightOffset;
            if (persistedPointLightCount >= LowPolyStreetAnimatedPointLightCount)
            {
                const std::vector<PointLight>& pointLights = lights.GetPointLights();
                m_InitialPointLights.insert(
                    m_InitialPointLights.end(),
                    pointLights.begin() + static_cast<std::ptrdiff_t>(m_PointLightOffset),
                    pointLights.begin() + static_cast<std::ptrdiff_t>(
                        m_PointLightOffset + LowPolyStreetAnimatedPointLightCount));
            }
            else
            {
                m_PointLightOffset = lights.GetPointLightCount();
                const XMMATRIX interleaveRotation = XMMatrixRotationY(LowPolyStreetPointLightAngularOffset);
                for (size_t lightIndex = 0; lightIndex < LowPolyStreetAnimatedPointLightCount; ++lightIndex)
                {
                    const AreaLightData& areaLight = m_InitialLights[lightIndex];
                    const XMFLOAT3 areaPosition = {
                        areaLight.PositionAndRange.x,
                        pointLightHeight,
                        areaLight.PositionAndRange.z
                    };
                    const XMFLOAT3 pointPosition = RotateAroundY(areaPosition, m_OrbitCenter, interleaveRotation);
                    PointLight pointLight(
                        { pointPosition.x, pointPosition.y, pointPosition.z, 1.0f },
                        LowPolyStreetPointLightRange);
                    pointLight.Color = {
                        areaLight.ColorAndIntensity.x,
                        areaLight.ColorAndIntensity.y,
                        areaLight.ColorAndIntensity.z,
                        LowPolyStreetPointLightIntensity
                    };
                    pointLight.SourceRadius = LowPolyStreetPointLightSourceRadius;
                    pointLight.RecalculateAttenuationCoefficients();
                    lights.AddPointLight(pointLight);
                    m_InitialPointLights.push_back(pointLight);
                }
            }
            m_ElapsedTime = 0.0f;
        }

        bool OnUpdate(
            SceneLightManager& lights,
            Camera& camera,
            const float deltaTime,
            const float,
            const bool updateCamera,
            const bool updateLights) override
        {
            if (m_InitialLights.size() != LowPolyStreetAnimatedAreaLightCount || deltaTime <= 0.0f)
            {
                return false;
            }

            m_ElapsedTime += std::clamp(deltaTime, 0.0f, 0.1f);
            if (updateCamera)
            {
                const float cameraOrbitAngle = m_CameraOrbitAngle;
                const float orbitRamp = std::clamp(
                    m_ElapsedTime / LowPolyStreetCameraOrbitRampDuration,
                    0.0f,
                    1.0f);
                const float smoothOrbitRamp = orbitRamp * orbitRamp * (3.0f - 2.0f * orbitRamp);
                const XMVECTOR cameraOffset = XMVectorAdd(
                    XMVectorScale(XMLoadFloat3(&m_CameraOrbitBasisU), std::cos(cameraOrbitAngle)),
                    XMVectorScale(XMLoadFloat3(&m_CameraOrbitBasisV), std::sin(cameraOrbitAngle)));
                XMFLOAT3 cameraPosition{};
                XMStoreFloat3(
                    &cameraPosition,
                    XMVectorAdd(
                        XMLoadFloat3(&m_InitialCameraPosition),
                        XMVectorScale(cameraOffset, m_CameraOrbitRadius * smoothOrbitRamp)));
                camera.SetLookAt(
                    XMLoadFloat3(&cameraPosition),
                    XMLoadFloat3(&m_LookAtTarget),
                    XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            }

            m_CameraOrbitAngle = std::fmod(
                m_CameraOrbitAngle + deltaTime * m_CameraAngularSpeed,
                XM_2PI);

            if (updateLights)
            {
                const XMMATRIX lightRotation = XMMatrixRotationY(m_ElapsedTime * LowPolyStreetLightAngularSpeed);
                for (size_t lightIndex = 0; lightIndex < m_InitialLights.size(); ++lightIndex)
                {
                    const AreaLightData& initial = m_InitialLights[lightIndex];
                    AreaLightData animated = lights.GetAreaLights().at(lightIndex);

                const XMFLOAT3 initialPosition = {
                    initial.PositionAndRange.x,
                    initial.PositionAndRange.y,
                    initial.PositionAndRange.z
                };
                const XMFLOAT3 position = RotateAroundY(initialPosition, m_OrbitCenter, lightRotation);
                const XMFLOAT3 normal = RotateDirectionAroundY(initial.NormalAndType, lightRotation);
                const XMFLOAT3 axisU = RotateDirectionAroundY(initial.AxisUAndExtent, lightRotation);
                const XMFLOAT3 axisV = RotateDirectionAroundY(initial.AxisVAndExtent, lightRotation);

                animated.PositionAndRange.x = position.x;
                animated.PositionAndRange.y = position.y;
                animated.PositionAndRange.z = position.z;
                animated.NormalAndType.x = normal.x;
                animated.NormalAndType.y = normal.y;
                animated.NormalAndType.z = normal.z;
                animated.AxisUAndExtent.x = axisU.x;
                animated.AxisUAndExtent.y = axisU.y;
                animated.AxisUAndExtent.z = axisU.z;
                animated.AxisVAndExtent.x = axisV.x;
                animated.AxisVAndExtent.y = axisV.y;
                animated.AxisVAndExtent.z = axisV.z;

                    lights.EditAreaLight(lightIndex) = animated;
                    lights.CommitAreaLightEdit(lightIndex, false);
                }

                for (size_t lightIndex = 0; lightIndex < m_InitialPointLights.size(); ++lightIndex)
                {
                    const size_t runtimeLightIndex = m_PointLightOffset + lightIndex;
                    if (runtimeLightIndex >= lights.GetPointLightCount())
                    {
                        break;
                    }

                    const PointLight& initial = m_InitialPointLights[lightIndex];
                    const XMFLOAT3 initialPosition = {
                        initial.PositionWs.x,
                        initial.PositionWs.y,
                        initial.PositionWs.z
                    };
                    const XMFLOAT3 position = RotateAroundY(initialPosition, m_OrbitCenter, lightRotation);
                    PointLight animated = lights.GetPointLights().at(runtimeLightIndex);
                    animated.PositionWs = { position.x, position.y, position.z, 1.0f };
                    lights.EditPointLight(runtimeLightIndex) = animated;
                    lights.CommitPointLightEdit(runtimeLightIndex, false);
                }
            }
            return updateCamera || updateLights;
        }

        bool AdjustCameraAngularSpeed(
            const float deltaRadiansPerSecond,
            float& newRadiansPerSecond) override
        {
            m_CameraAngularSpeed = std::clamp(
                m_CameraAngularSpeed + deltaRadiansPerSecond,
                LowPolyStreetCameraMinimumAngularSpeed,
                LowPolyStreetCameraMaximumAngularSpeed);
            newRadiansPerSecond = m_CameraAngularSpeed;
            return true;
        }

        bool SetCameraAngularSpeedToMaximum(float& newRadiansPerSecond) override
        {
            m_CameraAngularSpeed = LowPolyStreetCameraMaximumAngularSpeed;
            newRadiansPerSecond = m_CameraAngularSpeed;
            return true;
        }

        bool AdjustCameraConeHalfAngle(
            const float deltaRadians,
            float& newRadians) override
        {
            m_CameraConeHalfAngle = std::clamp(
                m_CameraConeHalfAngle + deltaRadians,
                LowPolyStreetCameraMinimumConeHalfAngle,
                LowPolyStreetCameraMaximumConeHalfAngle);
            m_CameraOrbitRadius = m_CameraAxisLength * std::tan(m_CameraConeHalfAngle);
            newRadians = m_CameraConeHalfAngle;
            return true;
        }

        void OnUnload(SceneLightManager&) override
        {
            m_InitialLights.clear();
            m_InitialPointLights.clear();
            m_ElapsedTime = 0.0f;
            m_CameraOrbitAngle = 0.0f;
        }

    private:
        std::vector<AreaLightData> m_InitialLights;
        std::vector<PointLight> m_InitialPointLights;
        XMFLOAT3 m_OrbitCenter = {};
        XMFLOAT3 m_InitialCameraPosition = {};
        XMFLOAT3 m_LookAtTarget = {};
        XMFLOAT3 m_CameraOrbitBasisU = {};
        XMFLOAT3 m_CameraOrbitBasisV = {};
        size_t m_PointLightOffset = 0;
        float m_CameraAxisLength = 0.0f;
        float m_CameraOrbitRadius = 0.0f;
        float m_CameraAngularSpeed = LowPolyStreetCameraAngularSpeed;
        float m_CameraConeHalfAngle = LowPolyStreetCameraConeHalfAngle;
        float m_CameraOrbitAngle = 0.0f;
        float m_ElapsedTime = 0.0f;
    };

    std::unique_ptr<RaytracingDemoSceneBehavior> CreateSceneBehavior(const Scene& scene)
    {
        if (NormalizeSceneStem(scene.GetSourcePath()) == "lowpolystreet")
        {
            return std::make_unique<LowPolyStreetSceneBehavior>();
        }
        return nullptr;
    }
}

RaytracingDemoSceneRuntimeController::RaytracingDemoSceneRuntimeController() = default;
RaytracingDemoSceneRuntimeController::~RaytracingDemoSceneRuntimeController() = default;

void RaytracingDemoSceneRuntimeController::SetStressTestSpheresEnabled(const bool enabled)
{
    if (m_StressTestSpheresEnabled == enabled)
    {
        return;
    }

    m_StressTestSpheresEnabled = enabled;
    m_StressTestSpheresStateDirty = true;
}

void RaytracingDemoSceneRuntimeController::LoadScene(
    const Scene& scene,
    SceneLightManager& lights,
    const Camera& camera)
{
    if (m_SceneBehavior != nullptr)
    {
        m_SceneBehavior->OnUnload(lights);
    }
    m_SceneBehavior = CreateSceneBehavior(scene);
    m_SceneBehaviorUpdateCount = 0;
    m_SceneCameraControlEnabled = true;
    m_SceneLightAnimationEnabled = true;
    if (m_SceneBehavior != nullptr)
    {
        m_SceneBehavior->OnLoad(lights, camera);
    }
}

bool RaytracingDemoSceneRuntimeController::Update(
    SceneLightManager& lights,
    Camera& camera,
    const float deltaTime,
    const float totalTime)
{
    bool renderInputsChanged = false;
    if (lights.IsPointLightAnimationEnabled() &&
        (m_SceneBehavior == nullptr || m_SceneLightAnimationEnabled))
    {
        lights.UpdateDynamicLights(totalTime);
        renderInputsChanged = true;
    }

    if (m_SceneBehaviorEnabled && m_SceneBehavior != nullptr &&
        m_SceneBehavior->OnUpdate(
            lights,
            camera,
            deltaTime,
            totalTime,
            m_SceneCameraControlEnabled,
            m_SceneLightAnimationEnabled))
    {
        ++m_SceneBehaviorUpdateCount;
        renderInputsChanged = true;
    }
    return renderInputsChanged;
}

std::string_view RaytracingDemoSceneRuntimeController::GetActiveSceneBehaviorName() const
{
    return m_SceneBehavior != nullptr ? m_SceneBehavior->GetName() : std::string_view{};
}

bool RaytracingDemoSceneRuntimeController::AdjustSceneCameraAngularSpeed(
    const float deltaRadiansPerSecond,
    float& newRadiansPerSecond)
{
    return m_SceneBehavior != nullptr &&
        m_SceneBehavior->AdjustCameraAngularSpeed(deltaRadiansPerSecond, newRadiansPerSecond);
}

bool RaytracingDemoSceneRuntimeController::SetSceneCameraAngularSpeedToMaximum(
    float& newRadiansPerSecond)
{
    return m_SceneBehavior != nullptr &&
        m_SceneBehavior->SetCameraAngularSpeedToMaximum(newRadiansPerSecond);
}

bool RaytracingDemoSceneRuntimeController::AdjustSceneCameraConeHalfAngle(
    const float deltaRadians,
    float& newRadians)
{
    return m_SceneBehavior != nullptr &&
        m_SceneBehavior->AdjustCameraConeHalfAngle(deltaRadians, newRadians);
}

bool RaytracingDemoSceneRuntimeController::ApplyPendingChanges(
    FrameworkDeviceContext& deviceContext,
    RaytracingDemoSceneResources& sceneResources,
    SceneLightManager& lights,
    DemoAutomation::RuntimeAutomationController& automation)
{
    if (!m_StressTestSpheresStateDirty)
    {
        return false;
    }

    m_StressTestSpheresStateDirty = false;
    if (sceneResources.AreStressTestSpheresEnabled() == m_StressTestSpheresEnabled)
    {
        return false;
    }

    automation.AppendDiagnosticLog("Stress transition: flush queues.");
    if (automation.IsRunning())
    {
        constexpr uint32_t automationFlushTimeoutMilliseconds = 10000u;
        if (!deviceContext.FlushWithTimeout(automationFlushTimeoutMilliseconds))
        {
            automation.AppendDiagnosticLog("Stress transition: queue flush timed out.");
            automation.FailNow(
                FrameworkDiagnostics::AutomationExitCode::Timeout,
                "Stress transition queue flush timed out after 10000 ms.");
            return false;
        }
    }
    else
    {
        deviceContext.Flush();
    }

    const std::shared_ptr<CommandQueue> commandQueue =
        deviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const std::shared_ptr<CommandList> commandList = commandQueue->GetCommandList();
    automation.AppendDiagnosticLog("Stress transition: update scene resources.");
    if (!sceneResources.SetStressTestSpheresEnabled(*commandList, m_StressTestSpheresEnabled))
    {
        return false;
    }

    automation.AppendDiagnosticLog("Stress transition: submit resource update.");
    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    automation.AppendDiagnosticLog("Stress transition: rebuild lights.");
    lights.SetEmissiveMeshSurfaceEmitters(sceneResources.CollectEmissiveMeshSurfaceEmitters());
    return true;
}
//Modify End
