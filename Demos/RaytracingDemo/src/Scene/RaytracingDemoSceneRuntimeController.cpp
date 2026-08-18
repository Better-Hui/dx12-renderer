//Modify Begin:2026-08-18 by Hui
#include <Scene/RaytracingDemoSceneRuntimeController.h>

#include <Automation/RuntimeAutomationController.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneResources.h>

#include <DX12Library/CommandQueue.h>
#include <Framework/Core/FrameworkDeviceContext.h>

void RaytracingDemoSceneRuntimeController::SetStressTestSpheresEnabled(const bool enabled)
{
    if (m_StressTestSpheresEnabled == enabled)
    {
        return;
    }

    m_StressTestSpheresEnabled = enabled;
    m_StressTestSpheresStateDirty = true;
}

bool RaytracingDemoSceneRuntimeController::UpdateAnimatedLights(
    SceneLightManager& lights,
    const float totalTime) const
{
    if (!lights.IsPointLightAnimationEnabled())
    {
        return false;
    }

    lights.UpdateDynamicLights(totalTime);
    return true;
}

bool RaytracingDemoSceneRuntimeController::ApplyPendingChanges(
    FrameworkDeviceContext& deviceContext,
    RaytracingDemoSceneResources& sceneResources,
    SceneLightManager& lights,
    const DemoAutomation::RuntimeAutomationController& automation)
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
    deviceContext.Flush();

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
