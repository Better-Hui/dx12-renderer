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
//Modify Begin:2026-07-30 by BestHui
    const RaytracingDemoFrameState& frameState = *config.FrameState;
//Modify End
    RaytracingDemoCameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, resources.SceneCamera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, resources.SceneCamera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, resources.SceneCamera.GetTranslation());
    camera.Width = context.m_Metadata.m_ScreenWidth;
    camera.Height = context.m_Metadata.m_ScreenHeight;
    camera.MaxBounces = static_cast<uint32_t>(std::clamp(frameState.MaxBounces, 0, 5));
    resources.Lights.FillCameraConstants(
        camera.DirectionalLightCount,
        camera.PointLightCount,
        camera.AreaLightCount,
        camera.SkyLight);
    camera.FrameIndex = frameState.FrameIndex;
    const RaytracingDemoLightingTechnique directLightingTechnique = frameState.DirectLightingTechnique;
    const RaytracingDemoLightingTechnique indirectLightingTechnique = frameState.IndirectLightingTechnique;
//Modify Begin:2026-08-06 by BestHui
    const PathTracingBackend backend = frameState.Backend;
    const bool restirDIEnabled =
        directLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI &&
        backend == PathTracingBackend::InlineRayQuery;
    const bool accumulationEnabled = frameState.AccumulationEnabled;
//Modify End
    camera.AccumulationFrameIndex = accumulationEnabled ? frameState.AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = accumulationEnabled ? 1u : 0u;
    resources.Denoisers.FillCameraConstants(camera.NRDDenoiserMode, camera.NRDReblurHitDistanceParameters);
    camera.DenoiserEnabled = static_cast<uint32_t>(resources.Denoisers.GetAlgorithm());
    const bool directLightingUsesReSTIRDI = restirDIEnabled;
    camera.DirectLightingActive =
        (directLightingTechnique == RaytracingDemoLightingTechnique::PathTracing || directLightingUsesReSTIRDI) ? 1u : 0u;
    camera.IndirectLightingActive = indirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing ? 1u : 0u;
//Modify Begin:2026-08-05 by BestHui
    camera.ReSTIRDIHistoryValid = frameState.ReSTIRDIHistoryValid ? 1u : 0u;
//Modify End
    return camera;
}

RaytracingDemoPipelineConstants BuildPassPipelineConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
//Modify Begin:2026-07-30 by BestHui
    const RaytracingDemoFrameState& frameState = *config.FrameState;
//Modify End
    RaytracingDemoPipelineConstants pipeline{};
    pipeline.View = resources.SceneCamera.GetViewMatrix();
    pipeline.Projection = resources.SceneCamera.GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, resources.SceneCamera.GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = {
        static_cast<float>(frameState.Width),
        static_cast<float>(frameState.Height)
    };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
    pipeline.PreviousViewProjection = frameState.HasPreviousViewProjection
        ? frameState.PreviousViewProjection
        : pipeline.ViewProjection;
    pipeline.DebugMeshletClusters = frameState.DebugMeshletClusters ? 1u : 0u;
    return pipeline;
}
//Modify End
