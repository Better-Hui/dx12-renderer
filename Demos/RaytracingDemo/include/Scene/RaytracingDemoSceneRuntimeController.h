#pragma once

//Modify Begin:2026-08-18 by Hui
class FrameworkDeviceContext;
class RaytracingDemoSceneResources;
class SceneLightManager;

namespace DemoAutomation
{
    class RuntimeAutomationController;
}

class RaytracingDemoSceneRuntimeController final
{
public:
    bool AreStressTestSpheresEnabled() const { return m_StressTestSpheresEnabled; }
    void SetStressTestSpheresEnabled(bool enabled);

    bool UpdateAnimatedLights(SceneLightManager& lights, float totalTime) const;
    bool ApplyPendingChanges(
        FrameworkDeviceContext& deviceContext,
        RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        DemoAutomation::RuntimeAutomationController& automation);

private:
    bool m_StressTestSpheresEnabled = false;
    bool m_StressTestSpheresStateDirty = false;
};
//Modify End
