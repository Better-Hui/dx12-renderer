#pragma once

//Modify Begin:2026-08-24 by Hui
#include <Denoising/DenoiserController.h>
#include <Passes/RaytracingDemoPassResources.h>
#include <RenderGraph/RenderGraphRoot.h>

#include <functional>
#include <memory>
#include <optional>

struct RaytracingDemoRenderGraphTopology
{
    bool DenoiserEnabled = false;
    DenoiserController::Algorithm DenoiserAlgorithm = DenoiserController::Algorithm::Off;
    NRD::DenoiserMode NRDDenoiserMode = NRD::DenoiserMode::ReblurDiffuse;
    uint32_t SVGFAtrousIterations = 1;
    bool BloomEnabled = false;
    CudaBloomPass::Backend BloomBackend = CudaBloomPass::Backend::Cuda;
    int BloomPyramidLevels = 1;
    bool DLSSEnabled = false;
    bool RayReconstructionEnabled = false;
    bool FrameGenerationEnabled = false;
    bool AsyncComputeEnabled = false;
    PathTracingBackend PathTracingBackendType = PathTracingBackend::InlineRayQuery;
    PathTracingDispatchMode PathTracingDispatchModeType = PathTracingDispatchMode::FullResolution;
    RaytracingDemoLightingTechnique DirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    RaytracingDemoLightingTechnique IndirectLightingTechnique = RaytracingDemoLightingTechnique::None;
    bool IndirectLightingEnabled = false;
    int LightingDebugTextureTarget = 0;
    bool MeshletGBufferEnabled = false;
    bool TaskMeshletEnabled = false;
    bool MeshletDebugEnabled = false;
    int DebugTextureTarget = 0;

    bool operator==(const RaytracingDemoRenderGraphTopology&) const = default;
};

struct RaytracingDemoRenderPipelineConfiguration
{
    RaytracingDemoRenderGraphTopology Topology;
    DLSSMode DlssMode = DLSSMode::Disabled;
    uint32_t RenderWidth = 1;
    uint32_t RenderHeight = 1;
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;

    bool operator==(const RaytracingDemoRenderPipelineConfiguration&) const = default;
};

class RaytracingDemoRenderPipelineController final
{
public:
    using BeforeRebuild = std::function<void()>;
    using GraphFactory = std::function<std::unique_ptr<RenderGraph::RenderGraphRoot>()>;
    using GraphConfigurator = std::function<void(RenderGraph::RenderGraphRoot&)>;

    static RaytracingDemoRenderPipelineConfiguration BuildConfiguration(
        const RaytracingDemoFrameState& frameState);
    bool NeedsRebuild(const RaytracingDemoRenderPipelineConfiguration& configuration) const;
    void Rebuild(
        const RaytracingDemoRenderPipelineConfiguration& configuration,
        const BeforeRebuild& beforeRebuild,
        const GraphFactory& graphFactory,
        const GraphConfigurator& graphConfigurator);

    bool HasRenderGraph() const { return m_RenderGraph != nullptr; }
    RenderGraph::RenderGraphRoot& GetRenderGraph();
    const RenderGraph::RenderGraphRoot& GetRenderGraph() const;
    void Reset();

private:
    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
    std::optional<RaytracingDemoRenderPipelineConfiguration> m_Configuration;
};
//Modify End
