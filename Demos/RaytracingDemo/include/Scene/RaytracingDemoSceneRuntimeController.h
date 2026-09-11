#pragma once

//Modify Begin:2026-09-10 by Hui
#include <cstdint>
#include <memory>
#include <string_view>

class Camera;
class FrameworkDeviceContext;
class RaytracingDemoSceneResources;
class RaytracingDemoSceneBehavior;
class Scene;
class SceneLightManager;

namespace DemoAutomation
{
    class RuntimeAutomationController;
}

class RaytracingDemoSceneRuntimeController final
{
public:
    RaytracingDemoSceneRuntimeController();
    ~RaytracingDemoSceneRuntimeController();

    bool AreStressTestSpheresEnabled() const { return m_StressTestSpheresEnabled; }
    void SetStressTestSpheresEnabled(bool enabled);

    void LoadScene(const Scene& scene, SceneLightManager& lights, const Camera& camera);
    bool Update(SceneLightManager& lights, Camera& camera, float deltaTime, float totalTime);
    bool HasActiveSceneBehavior() const { return m_SceneBehavior != nullptr; }
    bool IsSceneBehaviorEnabled() const { return m_SceneBehaviorEnabled; }
    void SetSceneBehaviorEnabled(bool enabled) { m_SceneBehaviorEnabled = enabled; }
    bool IsSceneCameraControlEnabled() const { return m_SceneCameraControlEnabled; }
    void SetSceneCameraControlEnabled(bool enabled) { m_SceneCameraControlEnabled = enabled; }
    bool IsSceneLightAnimationEnabled() const { return m_SceneLightAnimationEnabled; }
    void SetSceneLightAnimationEnabled(bool enabled) { m_SceneLightAnimationEnabled = enabled; }
    bool AdjustSceneCameraAngularSpeed(float deltaRadiansPerSecond, float& newRadiansPerSecond);
    bool SetSceneCameraAngularSpeedToMaximum(float& newRadiansPerSecond);
    bool AdjustSceneCameraConeHalfAngle(float deltaRadians, float& newRadians);
    std::string_view GetActiveSceneBehaviorName() const;
    uint64_t GetSceneBehaviorUpdateCount() const { return m_SceneBehaviorUpdateCount; }

    bool ApplyPendingChanges(
        FrameworkDeviceContext& deviceContext,
        RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        DemoAutomation::RuntimeAutomationController& automation);

private:
    std::unique_ptr<RaytracingDemoSceneBehavior> m_SceneBehavior;
    uint64_t m_SceneBehaviorUpdateCount = 0;
    bool m_SceneBehaviorEnabled = true;
    bool m_SceneCameraControlEnabled = true;
    bool m_SceneLightAnimationEnabled = true;
    bool m_StressTestSpheresEnabled = false;
    bool m_StressTestSpheresStateDirty = false;
};
//Modify End
