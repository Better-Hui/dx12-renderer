//Modify Begin:2026-08-26 by Hui
#include <Passes/RaytracingDemoPassResources.h>

#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderMetadata.h>
#include <Scene/SceneLightManager.h>

#include <DirectXMath.h>

#include <algorithm>

using namespace DirectX;

RaytracingDemoCameraConstants BuildPassCameraConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::RenderContext& context)
{
    const RaytracingDemoFrameState& frameState = *config.FrameState;
    RaytracingDemoCameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, frameState.View);
    camera.InverseProjection = XMMatrixInverse(nullptr, frameState.Projection);
    XMStoreFloat4(&camera.CameraPosition, resources.SceneCamera.GetTranslation());
    camera.Width = context.GetMetadata().m_ScreenWidth;
    camera.Height = context.GetMetadata().m_ScreenHeight;
    resources.Lights.FillCameraConstants(
        camera.DirectionalLightCount,
        camera.PointLightCount,
        camera.SpotLightCount,
        camera.SurfaceEmitterCount,
        camera.SkyLight);
    camera.FrameIndex = frameState.FrameIndex;
    const bool accumulationEnabled = frameState.AccumulationEnabled;
    camera.AccumulationFrameIndex = accumulationEnabled ? frameState.AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = accumulationEnabled ? 1u : 0u;
    resources.Denoisers.FillCameraConstants(camera.NRDDenoiserMode, camera.NRDReblurHitDistanceParameters);
    camera.ReSTIRDIHistoryValid = frameState.ReSTIRDIHistoryValid ? 1u : 0u;
    camera.UseSolidSkyFallback = resources.UseSolidSkyFallback ? 1u : 0u;
    return camera;
}

RaytracingDemoPipelineConstants BuildPassPipelineConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    const RaytracingDemoFrameState& frameState = *config.FrameState;
    RaytracingDemoPipelineConstants pipeline{};
    pipeline.View = frameState.View;
    pipeline.Projection = frameState.Projection;
    pipeline.ViewProjection = frameState.ViewProjection;
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
