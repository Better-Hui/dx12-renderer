//Modify Begin:2026-09-28 by Hui
#include <RaytracingDemo.h>
#include <Automation/RaytracingDemoAutomation.h>

#include <algorithm>
#include <stdexcept>

using RuntimeAutomationAction = RaytracingDemoAutomation::Action;
using RuntimeAutomationMatrixCase = RaytracingDemoAutomation::MatrixCase;

void RaytracingDemo::ApplyRuntimeAutomationMatrixCase(const uint32_t caseIndex)
{
    const auto& matrixCases = RaytracingDemoAutomation::GetMatrixCases();
    if (caseIndex >= matrixCases.size())
    {
        throw std::out_of_range("Runtime automation matrix case index is out of range.");
    }

    const RuntimeAutomationMatrixCase& testCase = matrixCases[caseIndex];
    m_PathTracingBackend = testCase.Backend;
    m_DirectLightingTechnique = testCase.DirectLighting;
    m_IndirectLightingTechnique = testCase.IndirectLighting;
    m_AsyncComputeEnabled = testCase.Backend == PathTracingBackend::InlineRayQuery && testCase.AsyncCompute;
    m_ParallelDirectCommandRecordingEnabled = testCase.ParallelDirectCommandRecording;
    m_UseMeshletGBuffer = testCase.UseMeshletGBuffer;
    m_UseTaskShaderMeshlets = testCase.UseTaskShaderMeshlets;
    m_SoftShadowsEnabled = testCase.SoftShadows;
    m_SceneRuntime.SetStressTestSpheresEnabled(testCase.StressSpheres);
    m_SkyboxEnabled = testCase.Skybox;
    m_AccumulationEnabled = testCase.Accumulation;
    m_DLSS.SetMode(testCase.DlssMode);
    m_MaterialShadingModel = testCase.ShadingModel;
    SetMaxBounces(testCase.MaxBounces);
    m_DebugMeshletClusters = false;
    m_DebugLightingTextureTarget = 0;
    m_DebugTextureTarget = 0;
    m_DebugSerializeAsyncCompute = false;
    ApplyRenderStateChange(
        AccumulationResetScope::AllHistory,
        RenderPipelineUpdate::RebuildPipelinesAndRebindResources);
}

bool RaytracingDemo::ApplyTopologyRuntimeAutomationAction(
    const uint32_t actionValue,
    const uint32_t value)
{
    const bool enabled = value != 0u;
    const auto action = static_cast<RuntimeAutomationAction>(actionValue);
    switch (action)
    {
    case RuntimeAutomationAction::StressSpheres:
        m_SceneRuntime.SetStressTestSpheresEnabled(enabled);
        return true;
    case RuntimeAutomationAction::MeshletGBuffer:
        m_UseMeshletGBuffer = enabled;
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::MeshletTaskShader:
        m_UseTaskShaderMeshlets = enabled;
        m_UseMeshletGBuffer = true;
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::PathTracingBackend:
        m_PathTracingBackend = static_cast<PathTracingBackend>(value);
        if (m_PathTracingBackend != PathTracingBackend::InlineRayQuery)
        {
            m_AsyncComputeEnabled = false;
        }
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::DirectLighting:
        m_DirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::IndirectLighting:
        m_IndirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(value);
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::CopyQueueValidation:
        m_CopyQueueValidationEnabled = enabled;
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::DynamicRayTracingUpdate:
        m_SceneResources.SetDynamicRayTracingUpdatesEnabled(enabled);
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::Denoiser:
        if (value > static_cast<uint32_t>(DenoiserController::Algorithm::OIDN))
        {
            throw std::out_of_range("Runtime automation denoiser selection is out of range.");
        }
        m_Denoisers.SetAlgorithm(static_cast<DenoiserController::Algorithm>(value));
        if (m_Denoisers.IsEnabled())
        {
            m_AccumulationEnabled = false;
        }
        ApplyRenderStateChange(AccumulationResetScope::DenoisersAndDLSS, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::SVGFAtrousIterations:
    {
        SVGF::Settings settings = m_Denoisers.GetSVGFSettings();
        settings.AtrousIterations = std::clamp(value, 1u, 8u);
        m_Denoisers.SetSVGFSettings(settings);
        ApplyRenderStateChange(AccumulationResetScope::DenoisersAndDLSS, RenderPipelineUpdate::None);
        return true;
    }
    case RuntimeAutomationAction::DLSS:
        m_DLSS.SetMode(static_cast<DLSSMode>(value));
        ApplyRenderStateChange(AccumulationResetScope::DenoisersAndDLSS, RenderPipelineUpdate::None);
        return true;
    case RuntimeAutomationAction::Skybox:
        m_SkyboxEnabled = enabled;
        ApplyRenderStateChange(AccumulationResetScope::AllHistory, RenderPipelineUpdate::None);
        return true;
    default:
        return false;
    }
}
//Modify End
