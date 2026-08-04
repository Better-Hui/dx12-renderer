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
    if (ImGui::Checkbox("Enable Direct Lighting", &m_DirectLightingEnabled))
    {
        ResetAccumulation();
    }
    if (ImGui::Checkbox("Enable Indirect Lighting", &m_IndirectLightingEnabled))
    {
        ResetAccumulation();
    }
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
    const bool fovChanged = ImGui::SliderFloat("FOV", &m_CameraFov, 12.0f, 90.0f, "%.1f");
    const bool rotateSpeedChanged = ImGui::SliderFloat("Mouse Rotate", &m_MouseRotateSpeed, 0.01f, 0.5f, "%.3f");
    const bool panSpeedChanged = ImGui::SliderFloat("Mouse Pan", &m_MousePanSpeed, 0.005f, 0.25f, "%.3f");
    const bool dollySpeedChanged = ImGui::SliderFloat("Mouse Dolly", &m_MouseDollySpeed, 0.005f, 0.25f, "%.3f");
    const bool wheelSpeedChanged = ImGui::SliderFloat("Wheel Dolly", &m_MouseWheelDollySpeed, 0.05f, 5.0f, "%.2f");
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-03 by BestHui
    if (m_Scene.GetSourcePath().extension() == ".unity" && ImGui::Button("Save Camera To Unity Scene"))
//Modify End
    {
        try
        {
            SaveCurrentCameraToUnityScene();
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
//Modify End

    if (bouncesChanged)
    {
        ResetAccumulation();
    }
    if (fovChanged)
    {
        const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
        GetSceneCamera().SetProjection(m_CameraFov, aspectRatio, 0.1f, 1000.0f);
        ResetAccumulation();
    }
    if (rotateSpeedChanged || panSpeedChanged || dollySpeedChanged || wheelSpeedChanged)
    {
        ResetAccumulation();
    }
    ImGui::End();
}
//Modify End
