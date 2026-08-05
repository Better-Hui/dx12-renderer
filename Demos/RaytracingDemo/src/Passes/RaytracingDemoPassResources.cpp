//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>

#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderMetadata.h>
//Modify Begin:2026-07-30 by BestHui
#include <Scene/SceneLightManager.h>
//Modify End

#include <DirectXMath.h>

#include <algorithm>

using namespace DirectX;

RaytracingDemoCameraConstants BuildPassCameraConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::RenderContext& context)
{
    RaytracingDemoCameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, resources.SceneCamera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, resources.SceneCamera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, resources.SceneCamera.GetTranslation());
    camera.Width = context.m_Metadata.m_ScreenWidth;
    camera.Height = context.m_Metadata.m_ScreenHeight;
    camera.MaxBounces = static_cast<uint32_t>(std::clamp(config.MaxBounces != nullptr ? *config.MaxBounces : 0, 0, 5));
    resources.Lights.FillCameraConstants(
        camera.DirectionalLightCount,
        camera.PointLightCount,
        camera.AreaLightCount,
        camera.SkyLight);
    camera.FrameIndex = config.FrameIndex != nullptr ? *config.FrameIndex : static_cast<uint32_t>(context.m_Metadata.m_FrameIndex);
    const RaytracingDemoLightingTechnique directLightingTechnique = config.DirectLightingTechnique != nullptr
        ? *config.DirectLightingTechnique
        : RaytracingDemoLightingTechnique::PathTracing;
    const RaytracingDemoLightingTechnique indirectLightingTechnique = config.IndirectLightingTechnique != nullptr
        ? *config.IndirectLightingTechnique
        : RaytracingDemoLightingTechnique::PathTracing;
    const bool denoiserEnabled = resources.Denoisers.IsEnabled();
    const bool directLightingUsesPathTracing =
        (config.DirectLightingEnabled == nullptr || !*config.DirectLightingEnabled) ||
        directLightingTechnique == RaytracingDemoLightingTechnique::PathTracing;
    const bool indirectLightingUsesPathTracing =
        (config.IndirectLightingEnabled == nullptr || !*config.IndirectLightingEnabled) ||
        indirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing;
    const bool originalPathTracingEnabled = directLightingUsesPathTracing && indirectLightingUsesPathTracing;
    const bool pathAccumulationEnabled =
        config.AccumulationEnabled != nullptr &&
        *config.AccumulationEnabled &&
        !denoiserEnabled &&
        originalPathTracingEnabled;
    camera.AccumulationFrameIndex = pathAccumulationEnabled && config.AccumulationFrameIndex != nullptr ? *config.AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = pathAccumulationEnabled ? 1u : 0u;
    resources.Denoisers.FillCameraConstants(camera.NRDDenoiserMode, camera.NRDReblurHitDistanceParameters);
    camera.DenoiserEnabled = static_cast<uint32_t>(resources.Denoisers.GetAlgorithm());
    const PathTracingBackend backend = config.Backend != nullptr ? *config.Backend : PathTracingBackend::InlineRayQuery;
    const bool directLightingUsesReSTIRDI = directLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI &&
        backend == PathTracingBackend::InlineRayQuery;
    camera.DirectLightingEnabled = config.DirectLightingEnabled != nullptr && *config.DirectLightingEnabled &&
        (directLightingTechnique == RaytracingDemoLightingTechnique::PathTracing || directLightingUsesReSTIRDI) ? 1u : 0u;
    camera.IndirectLightingEnabled = config.IndirectLightingEnabled != nullptr && *config.IndirectLightingEnabled &&
        indirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing ? 1u : 0u;
//Modify Begin:2026-08-05 by BestHui
    camera.ReSTIRDIHistoryValid = config.ReSTIRDIHistoryValid != nullptr && *config.ReSTIRDIHistoryValid ? 1u : 0u;
//Modify End
    return camera;
}

RaytracingDemoPipelineConstants BuildPassPipelineConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    RaytracingDemoPipelineConstants pipeline{};
    pipeline.View = resources.SceneCamera.GetViewMatrix();
    pipeline.Projection = resources.SceneCamera.GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, resources.SceneCamera.GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = {
        static_cast<float>(config.Width != nullptr ? *config.Width : 1),
        static_cast<float>(config.Height != nullptr ? *config.Height : 1)
    };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
    pipeline.PreviousViewProjection = config.HasPreviousViewProjection != nullptr && *config.HasPreviousViewProjection && config.PreviousViewProjection != nullptr
        ? *config.PreviousViewProjection
        : pipeline.ViewProjection;
    pipeline.DebugMeshletClusters = config.DebugMeshletClusters != nullptr && *config.DebugMeshletClusters ? 1u : 0u;
    return pipeline;
}
//Modify End
