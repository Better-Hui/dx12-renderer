//Modify Begin:2026-07-27 by BestHui
#include <RaytracingDemo.h>

#include <imgui.h>

void RaytracingDemo::OnImGui()
{
    ImGui::SetNextWindowSize(ImVec2(520.0f, 680.0f), ImGuiCond_FirstUseEver);
//Modify Begin:2026-07-31 by BestHui
    ImGui::Begin("Raytracing");
//Modify End
    ImGui::Text("GBuffer Path Tracing");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Resolution: %d x %d", m_Width, m_Height);
    ImGui::Text("Frame: %u", m_FrameIndex);
    ImGui::Text("Accumulation: %u", m_AccumulationFrameIndex);
    if (ImGui::Button("Save Scene"))
    {
        try
        {
            SaveCurrentScene();
        }
        catch (const std::exception& exception)
        {
            m_CameraSaveStatus = std::string("Save failed: ") + exception.what();
        }
    }
    if (!m_CameraSaveStatus.empty())
    {
        ImGui::TextWrapped("%s", m_CameraSaveStatus.c_str());
    }
//Modify Begin:2026-07-29 by BestHui
    if (m_GpuTimestampProfiler.IsAvailable())
    {
        if (ImGui::Checkbox("Enable RG Timing", &m_GpuTimingEnabled))
        {
            m_GpuTimestampSamples.clear();
            m_GpuTimestampDisplaySamples.clear();
//Modify Begin:2026-08-03 by BestHui
            m_AsyncComputeGpuTimestampSamples.clear();
            m_AsyncComputeGpuTimestampDisplaySamples.clear();
//Modify End
//Modify Begin:2026-08-03 by BestHui
            if (!m_GpuTimingEnabled)
            {
                m_RenderGraphTimingCaptureEnabled = false;
            }
            ClearRenderGraphTimingHistory();
//Modify End
        }
        if (m_GpuTimingEnabled)
        {
//Modify Begin:2026-08-03 by BestHui
            if (ImGui::Checkbox("Capture RG Timing History", &m_RenderGraphTimingCaptureEnabled))
            {
                ClearRenderGraphTimingHistory();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::SliderInt("History Frames", &m_RenderGraphTimingHistoryCapacity, 30, 3600))
            {
                while (m_RenderGraphTimingHistory.size() > static_cast<size_t>(m_RenderGraphTimingHistoryCapacity))
                {
                    m_RenderGraphTimingHistory.pop_front();
                }
            }
            if (ImGui::Button("Dump RG Timing CSV"))
            {
                DumpRenderGraphTimingHistory();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear RG Timing History"))
            {
                ClearRenderGraphTimingHistory();
            }
            ImGui::Text(
                "History: %zu/%d frames",
                m_RenderGraphTimingHistory.size(),
                m_RenderGraphTimingHistoryCapacity);
            if (!m_RenderGraphTimingExportStatus.empty())
            {
                ImGui::TextWrapped("%s", m_RenderGraphTimingExportStatus.c_str());
            }
//Modify End
//Modify Begin:2026-08-02 by BestHui
            ImGui::Text(
                "RG Direct Queue: gpu %.3f ms, cpu %.3f ms",
                m_GpuTimestampProfiler.GetLastFrameGpuMilliseconds(),
                m_LastRenderGraphCpuMilliseconds);
//Modify End
            ImGui::Text(
                "RG Async Compute Queue: gpu %.3f ms",
                m_AsyncComputeGpuTimestampProfiler.GetLastFrameGpuMilliseconds());
            if (!m_GpuTimestampDisplaySamples.empty() && ImGui::CollapsingHeader("GPU RG Timing: Direct"))
            {
//Modify Begin:2026-08-02 by BestHui
                ImGui::Text("gpu/cpu delta: since previous marker, gpu/cpu total: since RG begin");
                for (const GpuTimestampSample& sample : m_GpuTimestampDisplaySamples)
                {
                    ImGui::Text(
                        "%s: gpu %.3f/%.3f ms, cpu %.3f/%.3f ms",
                        sample.Name.c_str(),
                        sample.MillisecondsFromPrevious,
                        sample.MillisecondsFromFrameStart,
                        sample.CpuMillisecondsFromPrevious,
                        sample.CpuMillisecondsFromFrameStart);
                }
//Modify End
            }
            if (!m_AsyncComputeGpuTimestampDisplaySamples.empty() && ImGui::CollapsingHeader("GPU RG Timing: Async Compute"))
            {
                ImGui::Text("gpu/cpu delta: since previous marker, gpu/cpu total: since async queue begin");
                for (const GpuTimestampSample& sample : m_AsyncComputeGpuTimestampDisplaySamples)
                {
                    ImGui::Text(
                        "%s: gpu %.3f/%.3f ms, cpu %.3f/%.3f ms",
                        sample.Name.c_str(),
                        sample.MillisecondsFromPrevious,
                        sample.MillisecondsFromFrameStart,
                        sample.CpuMillisecondsFromPrevious,
                        sample.CpuMillisecondsFromFrameStart);
                }
            }
        }
    }
//Modify End

    if (ImGui::Checkbox("Enable Accumulation", &m_AccumulationEnabled))
    {
        ResetAccumulation();
    }
//Modify Begin:2026-08-05 by BestHui
    const char* directLightingTechniqueNames[] = { "None", "PathTracing", "ReSTIR DI" };
    int directLightingTechnique = static_cast<int>(m_DirectLightingTechnique);
    if (ImGui::Combo("Direct Lighting", &directLightingTechnique, directLightingTechniqueNames, IM_ARRAYSIZE(directLightingTechniqueNames)))
    {
        m_DirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(directLightingTechnique);
        ResetAccumulation();
    }
    if (m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI &&
        m_PathTracingBackend != PathTracingBackend::InlineRayQuery)
    {
        ImGui::TextDisabled("ReSTIR DI currently requires Inline Ray Query.");
    }
    else if (m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI)
    {
        ReSTIRDISettings restirSettings = m_DirectLightingReSTIRDI.GetSettings();
        int candidateCount = static_cast<int>(restirSettings.CandidateCount);
        int spatialNeighborCount = static_cast<int>(restirSettings.SpatialNeighborCount);
        int temporalMaxHistoryLength = static_cast<int>(restirSettings.TemporalMaxHistoryLength);
        bool settingsChanged = false;
        settingsChanged |= ImGui::SliderInt("RIS Candidates", &candidateCount, 1, 32);
        settingsChanged |= ImGui::Checkbox("RIS Visibility", &restirSettings.EnableCandidateVisibility);
        settingsChanged |= ImGui::Checkbox("Enable Temporal Reuse", &restirSettings.EnableTemporalResampling);
        settingsChanged |= ImGui::SliderInt("Temporal Max History", &temporalMaxHistoryLength, 1, 64);
        if (restirSettings.EnableTemporalResampling)
        {
            settingsChanged |= ImGui::Checkbox("Temporal Visibility", &restirSettings.EnableTemporalVisibility);
        }
        settingsChanged |= ImGui::Checkbox("Enable Boiling Filter", &restirSettings.EnableBoilingFilter);
        settingsChanged |= ImGui::SliderFloat("Boiling Filter Strength", &restirSettings.BoilingFilterStrength, 0.01f, 1.0f);
        settingsChanged |= ImGui::Checkbox("Enable Spatial Reuse", &restirSettings.EnableSpatialResampling);
        if (restirSettings.EnableSpatialResampling)
        {
            settingsChanged |= ImGui::Checkbox("Spatial Visibility", &restirSettings.EnableSpatialVisibility);
            settingsChanged |= ImGui::SliderInt("Spatial Neighbors", &spatialNeighborCount, 1, 16);
            settingsChanged |= ImGui::SliderFloat("Spatial Radius (pixels)", &restirSettings.SpatialSamplingRadius, 1.0f, 64.0f);
            settingsChanged |= ImGui::SliderFloat("Spatial Normal Threshold", &restirSettings.SpatialNormalSimilarityThreshold, -1.0f, 1.0f);
            settingsChanged |= ImGui::SliderFloat("Spatial Depth Threshold", &restirSettings.DepthSimilarityThreshold, 0.0f, 1.0f);
            settingsChanged |= ImGui::SliderFloat("Spatial Material Threshold", &restirSettings.MaterialSimilarityThreshold, 0.0f, 2.0f);
        }
        settingsChanged |= ImGui::Checkbox("Final Visibility", &restirSettings.EnableFinalVisibility);
        ImGui::TextDisabled("Each enabled visibility stage traces inline shadow rays only for its selected candidates.");
        if (settingsChanged)
        {
            restirSettings.CandidateCount = static_cast<uint32_t>(candidateCount);
            restirSettings.SpatialNeighborCount = static_cast<uint32_t>(spatialNeighborCount);
            restirSettings.TemporalMaxHistoryLength = static_cast<uint32_t>(temporalMaxHistoryLength);
            m_DirectLightingReSTIRDI.SetSettings(restirSettings);
            ResetAccumulation();
        }
    }
    const char* indirectLightingTechniqueNames[] = { "None", "PathTracing" };
    int indirectLightingTechnique = static_cast<int>(m_IndirectLightingTechnique);
    if (ImGui::Combo("Indirect Lighting", &indirectLightingTechnique, indirectLightingTechniqueNames, IM_ARRAYSIZE(indirectLightingTechniqueNames)))
    {
        m_IndirectLightingTechnique = static_cast<RaytracingDemoLightingTechnique>(indirectLightingTechnique);
        ResetAccumulation();
    }
//Modify End
//Modify Begin:2026-07-30 by BestHui
    if (ImGui::Checkbox("Enable Soft Shadows", &m_SoftShadowsEnabled))
    {
        EnsureRayTracingPipelines();
        ResetAccumulation();
    }
    ImGui::TextDisabled("Directional and point lights use soft-shadow variants; area lights already sample their surface.");
//Modify End
//Modify Begin:2026-07-30 by BestHui
    if (ImGui::Checkbox("Enable Stress Test Spheres (12,288)", &m_StressTestSpheresEnabled))
    {
        m_StressTestSpheresStateDirty = true;
    }
    ImGui::TextDisabled("Adds or removes instances; BLAS and static meshlet geometry stay resident.");
//Modify End
//Modify Begin:2026-08-03 by BestHui
    if (m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
    {
        if (ImGui::Checkbox("Use Async Compute for Indirect Lighting", &m_AsyncComputeEnabled))
        {
            ResetAccumulation();
        }
//Modify Begin:2026-07-30 by BestHui
        ImGui::Checkbox("Debug: CPU Serialize Async Compute", &m_DebugSerializeAsyncCompute);
        ImGui::TextDisabled("Diagnostic only: waits on the CPU after each async submission.");
//Modify End
    }
    else
    {
        ImGui::TextDisabled("Async Compute: Inline Ray Query only");
    }
//Modify Begin:2026-07-30 by BestHui
    const char* lightingDebugTargetNames[] = {
        "Off",
        "Indirect Lighting",
        "NRD Noisy Radiance",
        "NRD Denoised Radiance",
    };
    if (ImGui::Combo(
        "Debug Lighting Texture",
        &m_DebugLightingTextureTarget,
        lightingDebugTargetNames,
        4))
    {
        ResetAccumulation();
    }
    if (m_DebugLightingTextureTarget == 3 && !m_Denoisers.IsNRDEnabled())
    {
        ImGui::TextDisabled("NRD Denoised Radiance requires the NRD denoiser.");
    }
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
    if (ImGui::Checkbox("Use Meshlet GBuffer", &m_UseMeshletGBuffer))
    {
        ResetAccumulation();
    }
    if (ImGui::Checkbox("Debug Meshlet Clusters", &m_DebugMeshletClusters))
    {
        if (m_DebugMeshletClusters)
        {
            m_UseMeshletGBuffer = true;
        }
        ResetAccumulation();
    }
//Modify Begin:2026-07-31 by BestHui
    if (m_DebugMeshletClusters)
    {
        const char* debugTargetNames[] = { "GBuffer Albedo", "GBuffer Normal", "GBuffer Position", "Motion Vector" };
        if (ImGui::Combo("Debug Target", &m_DebugTextureTarget, debugTargetNames, 4))
        {
            ResetAccumulation();
        }
    }
//Modify End
//Modify Begin:2026-07-31 by BestHui
    const char* meshletBackendNames[] = { "Task Shader", "Compute Indirect" };
    int selectedMeshletBackend = m_UseTaskShaderMeshlets ? 0 : 1;
    if (ImGui::Combo("Meshlet Backend", &selectedMeshletBackend, meshletBackendNames, 2))
    {
        m_UseTaskShaderMeshlets = selectedMeshletBackend == 0;
        m_UseMeshletGBuffer = true;
        ResetAccumulation();
    }
//Modify End
//Modify End
    if (m_Denoisers.DrawImGui())
    {
        if (IsDenoiserEnabled())
        {
            m_AccumulationEnabled = false;
        }
        ResetAccumulation();
    }
    if (m_Lights.DrawImGui())
    {
        ResetAccumulation();
    }
//Modify Begin:2026-07-27 by BestHui
    if (m_CudaBloom.DrawImGui())
    {
        ResetAccumulation(false);
    }
//Modify End

    const char* modeNames[] = { "Inline Ray Query", "Shader Table DXR" };
    int selectedMode = static_cast<int>(m_PathTracingBackend);
    if (ImGui::Combo("Mode", &selectedMode, modeNames, 2))
    {
        m_PathTracingBackend = static_cast<PathTracingBackend>(selectedMode);
        ResetAccumulation();
    }
    const bool bouncesChanged = ImGui::SliderInt("Bounces", &m_MaxBounces, 0, 5);
//Modify Begin:2026-08-05 by BestHui
    bool fovChanged = false;
    bool nearClipChanged = false;
    bool farClipChanged = false;
    bool rotateSpeedChanged = false;
    bool panSpeedChanged = false;
    bool dollySpeedChanged = false;
    bool wheelSpeedChanged = false;
    if (ImGui::CollapsingHeader("Camera"))
    {
        fovChanged = ImGui::SliderFloat("FOV", &m_CameraFov, 12.0f, 90.0f, "%.1f");
        nearClipChanged = ImGui::DragFloat("Near Clip", &m_CameraNearClipPlane, 0.01f, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (m_CameraFarClipPlane <= m_CameraNearClipPlane)
        {
            m_CameraFarClipPlane = m_CameraNearClipPlane + 0.001f;
        }
        farClipChanged = ImGui::DragFloat("Far Clip", &m_CameraFarClipPlane, 1.0f, m_CameraNearClipPlane + 0.001f, 100000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        rotateSpeedChanged = ImGui::SliderFloat("Mouse Rotate", &m_MouseRotateSpeed, 0.01f, 0.5f, "%.3f");
        panSpeedChanged = ImGui::SliderFloat("Mouse Pan", &m_MousePanSpeed, 0.005f, 0.25f, "%.3f");
        dollySpeedChanged = ImGui::SliderFloat("Mouse Dolly", &m_MouseDollySpeed, 0.005f, 0.25f, "%.3f");
        wheelSpeedChanged = ImGui::SliderFloat("Wheel Dolly", &m_MouseWheelDollySpeed, 0.05f, 5.0f, "%.2f");
    }
//Modify End

    if (bouncesChanged)
    {
        ResetAccumulation();
    }
    if (fovChanged || nearClipChanged || farClipChanged)
    {
        const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
        GetSceneCamera().SetProjection(m_CameraFov, aspectRatio, m_CameraNearClipPlane, m_CameraFarClipPlane);
        ResetAccumulation();
    }
    if (rotateSpeedChanged || panSpeedChanged || dollySpeedChanged || wheelSpeedChanged)
    {
        ResetAccumulation();
    }
    ImGui::End();
}
//Modify End
