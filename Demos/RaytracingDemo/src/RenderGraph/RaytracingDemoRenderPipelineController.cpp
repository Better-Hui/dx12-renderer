//Modify Begin:2026-08-18 by Hui
#include <RenderGraph/RaytracingDemoRenderPipelineController.h>

#include <DX12Library/Helpers.h>

RaytracingDemoRenderPipelineConfiguration RaytracingDemoRenderPipelineController::BuildConfiguration(
    const RaytracingDemoFrameState& frameState)
{
    return {
        .Topology = {
            .DenoiserEnabled = frameState.DenoiserEnabled,
            .DenoiserAlgorithm = frameState.DenoiserAlgorithm,
            .NRDDenoiserMode = frameState.NRDDenoiserMode,
            .SVGFAtrousIterations = frameState.SVGFAtrousIterations,
            .BloomEnabled = frameState.BloomEnabled,
            .BloomBackend = frameState.BloomBackend,
            .BloomPyramidLevels = frameState.BloomBackend == BloomController::Backend::FrameworkRaster
                ? frameState.BloomPyramidLevels
                : 0,
            .DLSSEnabled = frameState.DLSSEnabled,
            .RayReconstructionEnabled = frameState.RayReconstructionEnabled,
            .FrameGenerationEnabled = frameState.FrameGenerationEnabled,
            .AsyncComputeEnabled = frameState.AsyncComputeEnabled,
//Modify Begin:2026-08-25 by Hui
            .CopyQueueValidationEnabled = frameState.CopyQueueValidationEnabled,
            .DynamicRayTracingUpdateEnabled = frameState.DynamicRayTracingUpdateEnabled,
//Modify End
            .PathTracingBackendType = frameState.Backend,
            .PathTracingDispatchModeType = frameState.DispatchMode,
            .DirectLightingTechnique = frameState.DirectLightingTechnique,
            .IndirectLightingTechnique = frameState.IndirectLightingTechnique,
            .IndirectLightingEnabled = frameState.MaxBounces > 1,
            .LightingDebugTextureTarget = frameState.DebugLightingTextureTarget,
            .MeshletGBufferEnabled = frameState.UseMeshletGBuffer,
            .TaskMeshletEnabled = frameState.UseTaskShaderMeshlets,
            .MeshletDebugEnabled = frameState.UseMeshletGBuffer && frameState.DebugMeshletClusters,
            .DebugTextureTarget = frameState.DebugTextureTarget,
        },
        .DlssMode = frameState.DlssMode,
        .RenderWidth = frameState.Width,
        .RenderHeight = frameState.Height,
        .DisplayWidth = frameState.DisplayWidth,
        .DisplayHeight = frameState.DisplayHeight,
    };
}

bool RaytracingDemoRenderPipelineController::NeedsRebuild(
    const RaytracingDemoRenderPipelineConfiguration& configuration) const
{
    return m_RenderGraph == nullptr || !m_Configuration.has_value() || *m_Configuration != configuration;
}

void RaytracingDemoRenderPipelineController::Rebuild(
    const RaytracingDemoRenderPipelineConfiguration& configuration,
    const BeforeRebuild& beforeRebuild,
    const GraphFactory& graphFactory,
    const GraphConfigurator& graphConfigurator)
{
    if (m_RenderGraph != nullptr)
    {
        beforeRebuild();
    }

    std::unique_ptr<RenderGraph::RenderGraphRoot> newRenderGraph = graphFactory();
    Assert(newRenderGraph != nullptr, "RaytracingDemo render-graph factory returned null.");
    graphConfigurator(*newRenderGraph);
    m_RenderGraph = std::move(newRenderGraph);
    m_Configuration = configuration;
}

RenderGraph::RenderGraphRoot& RaytracingDemoRenderPipelineController::GetRenderGraph()
{
    Assert(m_RenderGraph != nullptr, "RaytracingDemo render graph is not initialized.");
    return *m_RenderGraph;
}

const RenderGraph::RenderGraphRoot& RaytracingDemoRenderPipelineController::GetRenderGraph() const
{
    Assert(m_RenderGraph != nullptr, "RaytracingDemo render graph is not initialized.");
    return *m_RenderGraph;
}

void RaytracingDemoRenderPipelineController::Reset()
{
    m_RenderGraph.reset();
    m_Configuration.reset();
}
//Modify End
