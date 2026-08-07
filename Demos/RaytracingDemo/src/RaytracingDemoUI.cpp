//Modify Begin:2026-07-27 by BestHui
#include <RaytracingDemo.h>

#include <imgui.h>

#include <algorithm>

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
            m_RenderGraphTimingHistory.Clear();
//Modify End
        }
        if (m_GpuTimingEnabled)
        {
//Modify Begin:2026-08-03 by BestHui
            if (ImGui::Checkbox("Capture RG Timing History", &m_RenderGraphTimingCaptureEnabled))
            {
                m_RenderGraphTimingHistory.Clear();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            int timingHistoryCapacity = static_cast<int>(m_RenderGraphTimingHistory.GetCapacity());
            if (ImGui::SliderInt("History Frames", &timingHistoryCapacity, 30, 3600))
            {
                m_RenderGraphTimingHistory.SetCapacity(static_cast<size_t>(timingHistoryCapacity));
            }
            if (ImGui::Button("Dump RG Timing CSV"))
            {
                m_RenderGraphTimingHistory.DumpCsv();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear RG Timing History"))
            {
                m_RenderGraphTimingHistory.Clear();
            }
            ImGui::Text(
                "History: %zu/%d frames",
                m_RenderGraphTimingHistory.GetFrameCount(),
                static_cast<int>(m_RenderGraphTimingHistory.GetCapacity()));
            if (!m_RenderGraphTimingHistory.GetStatus().empty())
            {
                ImGui::TextWrapped("%s", m_RenderGraphTimingHistory.GetStatus().c_str());
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

//Modify Begin:2026-08-05 by BestHui
//Modify Begin:2026-07-30 by BestHui
    if (ImGui::CollapsingHeader("Lighting Algorithms"))
    {
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
        int spatialDisocclusionBoostSampleCount = static_cast<int>(restirSettings.SpatialDisocclusionBoostSampleCount);
        int spatialTargetHistoryLength = static_cast<int>(restirSettings.SpatialTargetHistoryLength);
        int temporalMaxHistoryLength = static_cast<int>(restirSettings.TemporalMaxHistoryLength);
        int finalVisibilityMaxAge = static_cast<int>(restirSettings.FinalVisibilityMaxAge);
        int temporalBiasCorrection = static_cast<int>(restirSettings.TemporalBiasCorrection);
        int spatialBiasCorrection = static_cast<int>(restirSettings.SpatialBiasCorrection);
        bool settingsChanged = false;
        if (ImGui::CollapsingHeader("ReSTIR DI Settings"))
        {
            if (ImGui::CollapsingHeader("Initial Sampling"))
            {
//Modify Begin:2026-08-06 by BestHui
                settingsChanged |= ImGui::Checkbox("RIS Initial Visibility", &restirSettings.EnableInitialVisibility);
//Modify End
                settingsChanged |= ImGui::DragInt("Local Light Samples", &candidateCount, 1.0f, 1, 64);
            }

            if (ImGui::CollapsingHeader("Temporal Resampling"))
            {
                settingsChanged |= ImGui::Checkbox("Enable Temporal Resampling", &restirSettings.EnableTemporalResampling);
                if (restirSettings.EnableTemporalResampling)
                {
//Modify Begin:2026-08-06 by BestHui
                    bool temporalRayTracedBiasCorrection = temporalBiasCorrection == static_cast<int>(ReSTIRDITemporalBiasCorrectionMode::RayTraced);
                    if (ImGui::Checkbox("Temporal Ray-Traced Bias Correction", &temporalRayTracedBiasCorrection))
                    {
                        temporalBiasCorrection = static_cast<int>(temporalRayTracedBiasCorrection
                            ? ReSTIRDITemporalBiasCorrectionMode::RayTraced
                            : ReSTIRDITemporalBiasCorrectionMode::Basic);
                        settingsChanged = true;
                    }
                    settingsChanged |= ImGui::Combo(
                        "Temporal Bias Correction",
                        &temporalBiasCorrection,
                        "Off\0Basic\0Ray Traced\0");
//Modify End
                    settingsChanged |= ImGui::DragInt("Temporal Max History Length", &temporalMaxHistoryLength, 1.0f, 1, 64);
                    settingsChanged |= ImGui::Checkbox("Enable Permutation Sampling", &restirSettings.EnableTemporalPermutationSampling);
                    settingsChanged |= ImGui::SliderFloat(
                        "Temporal Normal Threshold",
                        &restirSettings.TemporalNormalSimilarityThreshold,
                        -1.0f,
                        1.0f);
                    settingsChanged |= ImGui::SliderFloat(
                        "Temporal Depth Threshold",
                        &restirSettings.TemporalDepthSimilarityThreshold,
                        0.0f,
                        1.0f);
                }
                settingsChanged |= ImGui::Checkbox("Enable Boiling Filter", &restirSettings.EnableBoilingFilter);
                if (restirSettings.EnableBoilingFilter)
                {
                    settingsChanged |= ImGui::SliderFloat(
                        "Boiling Filter Strength",
                        &restirSettings.BoilingFilterStrength,
                        0.01f,
                        1.0f);
                }
            }

            if (ImGui::CollapsingHeader("Spatial Resampling"))
            {
                settingsChanged |= ImGui::Checkbox("Enable Spatial Resampling", &restirSettings.EnableSpatialResampling);
                if (restirSettings.EnableSpatialResampling)
                {
//Modify Begin:2026-08-06 by BestHui
                    bool spatialRayTracedBiasCorrection = spatialBiasCorrection == static_cast<int>(ReSTIRDISpatialBiasCorrectionMode::RayTraced);
                    if (ImGui::Checkbox("Spatial Ray-Traced Bias Correction", &spatialRayTracedBiasCorrection))
                    {
                        spatialBiasCorrection = static_cast<int>(spatialRayTracedBiasCorrection
                            ? ReSTIRDISpatialBiasCorrectionMode::RayTraced
                            : ReSTIRDISpatialBiasCorrectionMode::Basic);
                        settingsChanged = true;
                    }
                    settingsChanged |= ImGui::Combo(
                        "Spatial Bias Correction",
                        &spatialBiasCorrection,
                        "Off\0Basic\0Pairwise\0Ray Traced\0");
//Modify End
                    settingsChanged |= ImGui::DragInt("Spatial Samples", &spatialNeighborCount, 1.0f, 1, 32);
                    settingsChanged |= ImGui::DragInt(
                        "Spatial Disocclusion Boost Samples",
                        &spatialDisocclusionBoostSampleCount,
                        1.0f,
                        1,
                        32);
                    settingsChanged |= ImGui::DragInt(
                        "Spatial Target History Length",
                        &spatialTargetHistoryLength,
                        1.0f,
                        0,
                        64);
                    settingsChanged |= ImGui::SliderFloat(
                        "Spatial Sampling Radius",
                        &restirSettings.SpatialSamplingRadius,
                        1.0f,
                        64.0f);
                    settingsChanged |= ImGui::SliderFloat(
                        "Spatial Normal Threshold",
                        &restirSettings.SpatialNormalSimilarityThreshold,
                        -1.0f,
                        1.0f);
                    settingsChanged |= ImGui::SliderFloat(
                        "Spatial Depth Threshold",
                        &restirSettings.SpatialDepthSimilarityThreshold,
                        0.0f,
                        1.0f);
                    settingsChanged |= ImGui::Checkbox(
                        "Enable Spatial Material Similarity Test",
                        &restirSettings.EnableSpatialMaterialSimilarityTest);
                    if (restirSettings.EnableSpatialMaterialSimilarityTest)
                    {
                        settingsChanged |= ImGui::SliderFloat(
                            "Spatial Material Threshold",
                            &restirSettings.SpatialMaterialSimilarityThreshold,
                            0.0f,
                            2.0f);
                    }
                    settingsChanged |= ImGui::Checkbox(
                        "Discount Naive Spatial Samples",
                        &restirSettings.DiscountNaiveSpatialSamples);
                }
            }

            if (ImGui::CollapsingHeader("Final Shading"))
            {
//Modify Begin:2026-08-06 by BestHui
                settingsChanged |= ImGui::Checkbox("Final Shading Visibility", &restirSettings.EnableFinalVisibility);
//Modify End
                settingsChanged |= ImGui::Checkbox(
                    "Discard Invisible Samples",
                    &restirSettings.EnableTemporalVisibilityShortcut);
                settingsChanged |= ImGui::Checkbox(
                    "Reuse Final Visibility",
                    &restirSettings.ReuseFinalVisibility);
                if (restirSettings.ReuseFinalVisibility)
                {
                    settingsChanged |= ImGui::DragInt("Final Visibility Max Age", &finalVisibilityMaxAge, 1.0f, 0, 16);
                    settingsChanged |= ImGui::SliderFloat(
                        "Final Visibility Max Distance",
                        &restirSettings.FinalVisibilityMaxDistance,
                        0.0f,
                        127.0f);
                }
            }
        }
        if (settingsChanged)
        {
            restirSettings.CandidateCount = static_cast<uint32_t>(candidateCount < 1 ? 1 : candidateCount);
            restirSettings.TemporalBiasCorrection = static_cast<ReSTIRDITemporalBiasCorrectionMode>(temporalBiasCorrection);
            restirSettings.TemporalMaxHistoryLength = static_cast<uint32_t>(temporalMaxHistoryLength < 1 ? 1 : temporalMaxHistoryLength);
            restirSettings.SpatialBiasCorrection = static_cast<ReSTIRDISpatialBiasCorrectionMode>(spatialBiasCorrection);
            restirSettings.SpatialNeighborCount = static_cast<uint32_t>(spatialNeighborCount < 1 ? 1 : spatialNeighborCount);
            restirSettings.SpatialDisocclusionBoostSampleCount = static_cast<uint32_t>(
                spatialDisocclusionBoostSampleCount < 1 ? 1 : spatialDisocclusionBoostSampleCount);
            restirSettings.SpatialTargetHistoryLength = static_cast<uint32_t>(
                spatialTargetHistoryLength < 0 ? 0 : spatialTargetHistoryLength);
            restirSettings.FinalVisibilityMaxAge = static_cast<uint32_t>(finalVisibilityMaxAge < 0 ? 0 : finalVisibilityMaxAge);
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
    }
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
    if (ImGui::CollapsingHeader("Runtime Options"))
    {
    if (ImGui::Checkbox("Enable Accumulation", &m_AccumulationEnabled))
    {
        ResetAccumulation();
    }
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
//Modify Begin:2026-08-07 by BestHui
    ImGui::Checkbox("Enable Parallel Direct Command Recording", &m_ParallelDirectCommandRecordingEnabled);
    ImGui::TextDisabled("Current batch: Inline Ray Query direct and indirect lighting. GPU execution remains ordered.");
//Modify End
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
    }
//Modify End

//Modify Begin:2026-07-30 by BestHui
    if (ImGui::CollapsingHeader("Lights"))
    {
        if (m_LightEditor.Draw(m_Lights))
        {
            ResetAccumulation();
        }
    }
//Modify End

//Modify Begin:2026-08-07 by BestHui
    if (ImGui::CollapsingHeader("Upscaling"))
    {
        if (!m_DLSS.IsSupported())
        {
            ImGui::TextDisabled("DLSS unavailable: %s", m_DLSS.GetStatusMessage().c_str());
        }
        else
        {
            const char* dlssModeNames[] = { "Off", "DLAA", "Quality", "Balanced", "Performance", "Ultra Performance" };
            int dlssMode = static_cast<int>(m_DLSS.GetMode());
            if (ImGui::Combo("DLSS Super Resolution", &dlssMode, dlssModeNames, IM_ARRAYSIZE(dlssModeNames)))
            {
                m_DLSS.SetMode(static_cast<DLSSMode>(dlssMode));
                ResetAccumulation();
            }

            if (m_DLSS.IsEnabled())
            {
                const DLSSOptimalSettings settings = m_DLSS.GetOptimalSettings(
                    static_cast<uint32_t>((std::max)(m_Width, 1)),
                    static_cast<uint32_t>((std::max)(m_Height, 1)));
                ImGui::Text(
                    "Render resolution: %u x %u (display %d x %d)",
                    settings.RenderWidth,
                    settings.RenderHeight,
                    m_Width,
                    m_Height);
            }
        }
    }
//Modify End

//Modify Begin:2026-07-30 by BestHui
    if (ImGui::CollapsingHeader("Post-Processing"))
    {
    if (m_Denoisers.DrawImGui())
    {
        ResetAccumulation();
    }
//Modify Begin:2026-07-27 by BestHui
    if (m_CudaBloom.DrawImGui())
    {
        ResetAccumulation(false);
    }
//Modify End
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
