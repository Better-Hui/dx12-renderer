#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <utility>

//Modify Begin:2026-07-30 by Hui
void RaytracingDemoPasses::Builder::AddDenoisePass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    const DenoiserController::Algorithm algorithm = config.FrameState->DenoiserAlgorithm;
    Assert(algorithm != DenoiserController::Algorithm::Off, "Denoise pass requires an active algorithm.");

    if (algorithm == DenoiserController::Algorithm::NRD)
    {
        NRD::GraphInputs inputs = {};
        inputs.GBufferSpecularSmoothness = DemoResourceIds::GBufferSpecularSmoothness;
        inputs.GBufferNormal = DemoResourceIds::GBufferNormal;
        inputs.GBufferPosition = DemoResourceIds::GBufferPosition;
        inputs.Depth = DemoResourceIds::DepthBuffer;
        inputs.MotionVector = DemoResourceIds::MotionVector;
        inputs.NoisyRadiance = DemoResourceIds::NRDNoisyRadiance;
        inputs.GBufferAlbedoOcclusion = DemoResourceIds::GBufferAlbedoOcclusion;
        inputs.GBufferEmissionMetallic = DemoResourceIds::GBufferEmissionMetallic;
        inputs.NormalRoughness = DemoResourceIds::NRDNormalRoughness;
        inputs.ViewZ = DemoResourceIds::NRDViewZ;
        inputs.Motion = DemoResourceIds::NRDMotion;
        inputs.DenoisedRadiance = DemoResourceIds::NRDDenoisedRadiance;
        inputs.Output = DemoResourceIds::SceneColor;
        inputs.InputToken = DemoResourceIds::RayTracingFinishedToken;
        inputs.OutputToken = DemoResourceIds::DenoiseFinishedToken;
        inputs.Width = config.FrameState->Width;
        inputs.Height = config.FrameState->Height;
        inputs.ResolveFrameMatrices = [frameState = config.FrameState]()
        {
            return NRD::FrameMatrices{
                frameState->View,
                frameState->Projection,
            };
        };
        inputs.DiagnosticNamePrefix = L"RaytracingDemo.NRD";
        resources.Denoisers.AddNRDPasses(renderGraphBuilder, std::move(inputs));
        return;
    }

    SVGF::GraphInputs inputs = {};
    inputs.NoisyRadiance = DemoResourceIds::NoisyRadiance;
    inputs.GBufferNormal = DemoResourceIds::GBufferNormal;
    inputs.GBufferPosition = DemoResourceIds::GBufferPosition;
    inputs.MotionVector = DemoResourceIds::MotionVector;
    inputs.Depth = DemoResourceIds::DepthBuffer;
    inputs.Output = DemoResourceIds::SceneColor;
    inputs.InputToken = DemoResourceIds::RayTracingFinishedToken;
    inputs.OutputToken = DemoResourceIds::DenoiseFinishedToken;
    inputs.Width = config.FrameState->Width;
    inputs.Height = config.FrameState->Height;
    inputs.WidthExpression = [](const RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
    inputs.HeightExpression = [](const RenderMetadata& metadata) { return metadata.m_ScreenHeight; };
    inputs.ResolveFrameIndex = [frameState = config.FrameState]()
    {
        return static_cast<uint64_t>(frameState->FrameIndex);
    };
    inputs.DiagnosticNamePrefix = L"RaytracingDemo.SVGF";
    resources.Denoisers.AddSVGFPasses(renderGraphBuilder, std::move(inputs));
}
//Modify End
