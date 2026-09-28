//Modify Begin:2026-09-28 by Hui
#include <RaytracingDemo.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <stdexcept>

void RaytracingDemo::VerifyActiveRayTracedPixelCount()
{
    const auto failActivePixelAssertion = [this](std::string message)
    {
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "active_pixel_dispatch",
                FrameworkDiagnostics::AssertionResult::Failed,
                message);
        }
        throw std::runtime_error(std::move(message));
    };
    if (m_PathTracingDispatchMode != PathTracingDispatchMode::CompactedIndirect)
    {
        m_RuntimeAutomation.AppendDiagnosticLog("Active ray-traced pixel verification skipped: full-resolution dispatch.");
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "active_pixel_dispatch",
                FrameworkDiagnostics::AssertionResult::Unknown,
                "Verification is not applicable to full-resolution dispatch.");
        }
        return;
    }

    const ActivePixelReadbackStatus readbackStatus = m_ActivePixels.GetCountReadbackStatus();
    if (readbackStatus == ActivePixelReadbackStatus::NotQueued)
    {
        failActivePixelAssertion("Compacted ray-traced pixel dispatch readback was not queued.");
    }
    if (readbackStatus == ActivePixelReadbackStatus::NotCompleted)
    {
        failActivePixelAssertion("Compacted ray-traced pixel dispatch readback did not complete.");
    }

    const std::optional<ActivePixelDispatchDiagnostics> diagnostics = m_ActivePixels.GetLatestDiagnostics();
    if (!diagnostics.has_value())
    {
        failActivePixelAssertion("Completed compacted ray-traced pixel dispatch readback has no diagnostics.");
    }
    if (!diagnostics->HasConsistentDispatchArguments())
    {
        failActivePixelAssertion(
            "Compacted ray-traced pixel dispatch arguments do not match the active-pixel count: count=" +
            std::to_string(diagnostics->ActivePixelCount) +
            ", dispatch=(" + std::to_string(diagnostics->DispatchX) + ", " +
            std::to_string(diagnostics->DispatchY) + ", " +
            std::to_string(diagnostics->DispatchZ) + ").");
    }
    if (diagnostics->ActivePixelCount == 0u &&
        (m_DirectLightingTechnique != RaytracingDemoLightingTechnique::None ||
            (m_IndirectLightingTechnique != RaytracingDemoLightingTechnique::None && m_MaxBounces > 1)))
    {
        failActivePixelAssertion("Compacted ray-traced pixel count unexpectedly returned zero.");
    }

    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "active_pixel_dispatch",
            FrameworkDiagnostics::AssertionResult::Passed,
            "Active-pixel count and finalized indirect dispatch arguments agree.",
            {
                { "active_pixel_count", static_cast<uint64_t>(diagnostics->ActivePixelCount) },
                { "dispatch_x", static_cast<uint64_t>(diagnostics->DispatchX) },
                { "dispatch_y", static_cast<uint64_t>(diagnostics->DispatchY) },
                { "dispatch_z", static_cast<uint64_t>(diagnostics->DispatchZ) },
            });
    }

    m_RuntimeAutomation.AppendDiagnosticLog(
        "Latest completed active ray-traced pixels: " + std::to_string(diagnostics->ActivePixelCount) +
        "; dispatch: (" + std::to_string(diagnostics->DispatchX) + ", " +
        std::to_string(diagnostics->DispatchY) + ", " +
        std::to_string(diagnostics->DispatchZ) + ").");
}

void RaytracingDemo::VerifyCopyQueueValidation()
{
    const auto failCopyQueueAssertion = [this](std::string message)
    {
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "copy_queue_validation",
                FrameworkDiagnostics::AssertionResult::Failed,
                message);
        }
        throw std::runtime_error(std::move(message));
    };

    if (!m_CopyQueueValidationEnabled)
    {
        failCopyQueueAssertion("Copy queue validation was disabled before verification.");
    }

    const RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
    const RenderGraph::RenderGraphCrossQueuePlanValidation& plan =
        renderGraph.GetCrossQueuePlanValidation();
    const RenderGraph::RenderGraphQueueSynchronizationStats& synchronization =
        renderGraph.GetFrameSynchronizationStats();
    const RenderGraph::RenderGraphQueueFenceValues frameFences =
        renderGraph.GetFrameSubmissionFences();
    const RenderGraph::RenderGraphQueueFenceValues copiedColorRetirement =
        renderGraph.GetResourceRetirement(
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueValidationColor);
    const RenderGraph::RenderGraphQueueFenceValues computeColorRetirement =
        renderGraph.GetResourceRetirement(
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueValidationComputeColor);
    const RenderGraph::RenderGraphQueueRuntimeValidation& runtimeValidation =
        renderGraph.GetFrameRuntimeValidation();
    const bool aliasResourcesShareHeap = renderGraph.ShareTransientHeap(
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueAliasDirectScratch,
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueAliasCopyScratch) &&
        renderGraph.ShareTransientHeap(
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueAliasCopyScratch,
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueAliasComputeScratch);
    const RenderGraph::RenderGraphQueueFenceValues aliasHeapRetirement =
        renderGraph.GetTransientHeapRetirement(
            RaytracingDemoRenderGraph::ResourceIds::CopyQueueAliasComputeScratch);

    const bool passed =
        plan.IsValid() &&
        plan.CopyPassCount >= 1u &&
        plan.DirectToCopyTransferCount >= 1u &&
        plan.CopyToConsumerTransferCount >= 1u &&
        frameFences.Copy != 0u &&
        frameFences.AsyncCompute != 0u &&
        synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::Copy) >= 1u &&
        synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::AsyncCompute) >= 1u &&
        synchronization.GetWaitCount(RenderGraph::RenderPassQueue::Direct, RenderGraph::RenderPassQueue::Copy) >= 1u &&
        synchronization.GetWaitCount(RenderGraph::RenderPassQueue::Copy, RenderGraph::RenderPassQueue::Direct) >= 1u &&
        synchronization.GetWaitCount(RenderGraph::RenderPassQueue::Direct, RenderGraph::RenderPassQueue::AsyncCompute) >= 1u &&
        synchronization.GetWaitCount(RenderGraph::RenderPassQueue::AsyncCompute, RenderGraph::RenderPassQueue::Direct) >= 1u &&
        copiedColorRetirement.Copy != 0u &&
        copiedColorRetirement.AsyncCompute != 0u &&
        computeColorRetirement.AsyncCompute != 0u &&
        computeColorRetirement.Direct != 0u &&
        plan.CrossQueueAliasingCount >= 2u &&
        plan.MissingAliasingHappensBeforeCount == 0u &&
        runtimeValidation.IsValid() &&
        runtimeValidation.CrossQueueAliasHandoffCount >= 2u &&
        aliasResourcesShareHeap &&
        aliasHeapRetirement.Direct != 0u &&
        aliasHeapRetirement.Copy != 0u &&
        aliasHeapRetirement.AsyncCompute != 0u;
    const std::string message =
        "Copy queue handoff: cross_queue_transfers=" +
        std::to_string(plan.CrossQueueResourceTransferCount) +
        ", missing_state_transitions=" + std::to_string(plan.MissingStatePlanTransitionCount) +
        ", incorrect_state_transitions=" + std::to_string(plan.IncorrectStatePlanTransitionCount) +
        ", cross_queue_aliases=" + std::to_string(plan.CrossQueueAliasingCount) +
        ", runtime_alias_handoffs=" + std::to_string(runtimeValidation.CrossQueueAliasHandoffCount) +
        ", alias_heap_shared=" + std::string(aliasResourcesShareHeap ? "true" : "false") +
        ", copy_fence=" + std::to_string(frameFences.Copy) +
        ", async_fence=" + std::to_string(frameFences.AsyncCompute) + ".";
    if (!passed)
    {
        failCopyQueueAssertion(message);
    }

    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "copy_queue_validation",
            FrameworkDiagnostics::AssertionResult::Passed,
            message,
            {
                { "cross_queue_transfer_count", plan.CrossQueueResourceTransferCount },
                { "copy_pass_count", plan.CopyPassCount },
                { "direct_to_copy_transfer_count", plan.DirectToCopyTransferCount },
                { "copy_to_consumer_transfer_count", plan.CopyToConsumerTransferCount },
                { "copy_submission_count", synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::Copy) },
                { "async_submission_count", synchronization.GetSubmissionCount(RenderGraph::RenderPassQueue::AsyncCompute) },
                { "copy_retirement_fence", copiedColorRetirement.Copy },
                { "async_retirement_fence", copiedColorRetirement.AsyncCompute },
                { "direct_consumer_retirement_fence", computeColorRetirement.Direct },
                { "cross_queue_aliasing_count", plan.CrossQueueAliasingCount },
                { "runtime_alias_handoff_count", runtimeValidation.CrossQueueAliasHandoffCount },
                { "alias_heap_shared", aliasResourcesShareHeap },
                { "alias_heap_direct_retirement_fence", aliasHeapRetirement.Direct },
                { "alias_heap_copy_retirement_fence", aliasHeapRetirement.Copy },
                { "alias_heap_async_retirement_fence", aliasHeapRetirement.AsyncCompute },
            });
    }
    m_RuntimeAutomation.AppendDiagnosticLog(message);
}

void RaytracingDemo::VerifyDynamicRayTracingUpdate(const uint32_t value)
{
    const auto failDynamicRtasAssertion = [this](std::string message)
    {
        if (m_Diagnostics.IsEnabled())
        {
            m_Diagnostics.RecordAssertion(
                "dynamic_rtas_update",
                FrameworkDiagnostics::AssertionResult::Failed,
                message);
        }
        throw std::runtime_error(std::move(message));
    };

    const RaytracingDemoDynamicRtasUpdateStatistics& sceneStats =
        m_SceneResources.GetDynamicRayTracingUpdateStatistics();
    const RayTracingAccelerationStructureUpdateStatistics& accelerationStats =
        m_SceneResources.GetRayTracingAccelerationStructure().GetUpdateStatistics();
    const bool verifyRestore = value != 0u;
    const bool dynamicEmitterActive = m_SceneResources.HasActiveDynamicRayTracingEmitter();
    const uint64_t minimumUpdateCount = verifyRestore ? 4u : 3u;
    const bool passed =
        sceneStats.GeometryUploadCount >= minimumUpdateCount &&
        sceneStats.MeshletTransformUpdateCount >= minimumUpdateCount &&
        sceneStats.MeshletGeometryUpdateCount >= minimumUpdateCount &&
        (!dynamicEmitterActive || sceneStats.EmissiveMeshRefreshCount >= minimumUpdateCount) &&
        sceneStats.RefitCount >= minimumUpdateCount &&
        accelerationStats.BottomLevelUpdateCount >= minimumUpdateCount &&
        accelerationStats.TopLevelUpdateCount >= minimumUpdateCount &&
        accelerationStats.RetiredResourceCount >= minimumUpdateCount &&
        (!verifyRestore || (sceneStats.RestoreCount >= 1u && sceneStats.LastUpdateRestored));
    const std::string message =
        "Dynamic RTAS update: geometry_uploads=" + std::to_string(sceneStats.GeometryUploadCount) +
        ", meshlet_transform_updates=" + std::to_string(sceneStats.MeshletTransformUpdateCount) +
        ", meshlet_geometry_updates=" + std::to_string(sceneStats.MeshletGeometryUpdateCount) +
        ", emissive_mesh_refreshes=" + std::to_string(sceneStats.EmissiveMeshRefreshCount) +
        ", dynamic_emitter_active=" + std::to_string(dynamicEmitterActive) +
        ", refits=" + std::to_string(sceneStats.RefitCount) +
        ", blas_updates=" + std::to_string(accelerationStats.BottomLevelUpdateCount) +
        ", tlas_updates=" + std::to_string(accelerationStats.TopLevelUpdateCount) +
        ", retired_resources=" + std::to_string(accelerationStats.RetiredResourceCount) +
        ", restores=" + std::to_string(sceneStats.RestoreCount) + ".";
    if (!passed)
    {
        failDynamicRtasAssertion(message);
    }

    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "dynamic_rtas_update",
            FrameworkDiagnostics::AssertionResult::Passed,
            message,
            {
                { "geometry_upload_count", sceneStats.GeometryUploadCount },
                { "meshlet_transform_update_count", sceneStats.MeshletTransformUpdateCount },
                { "meshlet_geometry_update_count", sceneStats.MeshletGeometryUpdateCount },
                { "emissive_mesh_refresh_count", sceneStats.EmissiveMeshRefreshCount },
                { "refit_count", sceneStats.RefitCount },
                { "restore_count", sceneStats.RestoreCount },
                { "blas_update_count", accelerationStats.BottomLevelUpdateCount },
                { "tlas_update_count", accelerationStats.TopLevelUpdateCount },
                { "tlas_build_count", accelerationStats.TopLevelBuildCount },
                { "retired_resource_count", accelerationStats.RetiredResourceCount },
                { "verified_restore", verifyRestore },
            });
    }
    m_RuntimeAutomation.AppendDiagnosticLog(message);
}

void RaytracingDemo::VerifyDynamicSkinnedMeshCapability()
{
    const RaytracingDemoDynamicSceneCapabilities& capabilities =
        m_SceneResources.GetDynamicSceneCapabilities();
    const bool passed =
        capabilities.SupportsMeshletTransformUpdates &&
        capabilities.SupportsMeshletGeometryUpdates &&
        capabilities.SupportsDynamicEmissiveMeshUpdates &&
        !capabilities.SupportsSkinnedMeshUpdates;
    const std::string message =
        "Dynamic scene capability: meshlet_transform=enabled, meshlet_geometry=enabled, "
        "dynamic_emissive_mesh=enabled, skinned_mesh=explicitly_unsupported.";
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "dynamic_skinned_mesh_capability",
            passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
            message,
            {
                { "meshlet_transform_updates", capabilities.SupportsMeshletTransformUpdates },
                { "meshlet_geometry_updates", capabilities.SupportsMeshletGeometryUpdates },
                { "dynamic_emissive_mesh_updates", capabilities.SupportsDynamicEmissiveMeshUpdates },
                { "skinned_mesh_updates", capabilities.SupportsSkinnedMeshUpdates },
            });
    }
    if (!passed)
    {
        throw std::runtime_error(message);
    }
    m_RuntimeAutomation.AppendDiagnosticLog(message);
}

void RaytracingDemo::VerifyOIDNResult()
{
    const bool passed =
        m_Denoisers.IsOIDNEnabled() &&
        IsAccumulationActive() &&
        m_Denoisers.HasOIDNResult();
    const std::string message = passed
        ? "OIDN holds the denoised result while static after D3D12 shared-memory CUDA denoise and RenderGraph composite."
        : "OIDN did not produce an uploaded result before the automation verification deadline.";
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "oidn_async_pipeline",
            passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
            message,
            {
                { "denoiser_enabled", m_Denoisers.IsOIDNEnabled() },
                { "manual_accumulation_enabled", m_AccumulationEnabled },
                { "effective_accumulation_enabled", IsAccumulationActive() },
                { "backend", m_Denoisers.IsOIDNUsingCuda() ? "cuda" : "cpu_fallback" },
                { "result_uploaded", m_Denoisers.HasOIDNResult() },
            });
    }
    if (!passed)
    {
        throw std::runtime_error(message);
    }
    m_RuntimeAutomation.AppendDiagnosticLog(message);
}

void RaytracingDemo::VerifyOIDNInvalidated()
{
    const bool passed =
        m_Denoisers.IsOIDNEnabled() &&
        IsAccumulationActive() &&
        m_OIDNGenerationAfterCameraMotion > m_OIDNGenerationBeforeCameraMotion;
    const std::string message = passed
        ? "OIDN invalidated the prior result after camera motion and restarted static accumulation."
        : "OIDN did not advance its static-image generation after camera motion.";
    if (m_Diagnostics.IsEnabled())
    {
        m_Diagnostics.RecordAssertion(
            "oidn_motion_invalidation",
            passed ? FrameworkDiagnostics::AssertionResult::Passed : FrameworkDiagnostics::AssertionResult::Failed,
            message,
            {
                { "denoiser_enabled", m_Denoisers.IsOIDNEnabled() },
                { "effective_accumulation_enabled", IsAccumulationActive() },
                { "generation_before", m_OIDNGenerationBeforeCameraMotion },
                { "generation_after", m_OIDNGenerationAfterCameraMotion },
                { "result_uploaded", m_Denoisers.HasOIDNResult() },
            });
    }
    if (!passed)
    {
        throw std::runtime_error(message);
    }
    m_RuntimeAutomation.AppendDiagnosticLog(message);
}
//Modify End
