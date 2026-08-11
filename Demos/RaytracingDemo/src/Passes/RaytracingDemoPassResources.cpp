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
//Modify Begin:2026-08-07 by BestHui
    camera.InverseView = XMMatrixInverse(nullptr, frameState.View);
    camera.InverseProjection = XMMatrixInverse(nullptr, frameState.Projection);
//Modify End
    XMStoreFloat4(&camera.CameraPosition, resources.SceneCamera.GetTranslation());
    camera.Width = context.GetMetadata().m_ScreenWidth;
    camera.Height = context.GetMetadata().m_ScreenHeight;
//Modify Begin:2026-08-10 by BestHui
    camera.MaxBounces = static_cast<uint32_t>(std::clamp(frameState.MaxBounces, 1, 5));
//Modify End
    resources.Lights.FillCameraConstants(
        camera.DirectionalLightCount,
        camera.PointLightCount,
        camera.SurfaceEmitterCount,
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
//Modify Begin:2026-08-07 by BestHui
    camera.DenoiserEnabled = frameState.RayReconstructionEnabled
        ? 0u
        : static_cast<uint32_t>(resources.Denoisers.GetAlgorithm());
//Modify End
    const bool directLightingUsesReSTIRDI = restirDIEnabled;
    camera.DirectLightingActive =
        (directLightingTechnique == RaytracingDemoLightingTechnique::PathTracing || directLightingUsesReSTIRDI) ? 1u : 0u;
//Modify Begin:2026-08-10 by BestHui
    const bool indirectLightingUsesReSTIRGI =
        indirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        backend == PathTracingBackend::InlineRayQuery;
    camera.IndirectLightingActive =
        frameState.MaxBounces > 1 &&
        (indirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing || indirectLightingUsesReSTIRGI)
            ? 1u
            : 0u;
//Modify End
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
//Modify Begin:2026-08-07 by BestHui
    pipeline.View = frameState.View;
    pipeline.Projection = frameState.Projection;
    pipeline.ViewProjection = frameState.ViewProjection;
//Modify End
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
