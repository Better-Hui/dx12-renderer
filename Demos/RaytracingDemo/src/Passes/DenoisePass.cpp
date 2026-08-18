#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderGraphBuilder.h>

//Modify Begin:2026-08-18 by Hui
namespace
{
    struct DenoisePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        DenoiserController::Algorithm Algorithm = DenoiserController::Algorithm::Off;
    };
}
//Modify End

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

    renderGraphBuilder.AddPass<DenoisePassData>(
        L"Denoise",
        [&resources, algorithm](RenderGraphPassBuilder& passBuilder, DenoisePassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Algorithm = algorithm;
            passBuilder.ReadToken(DemoResourceIds::RayTracingFinishedToken);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferNormal);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferPosition);
            passBuilder.ReadBuffer(DemoResourceIds::MotionVector);
            passBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::SceneColor);
            passBuilder.WriteToken(DemoResourceIds::DenoiseFinishedToken);
            if (algorithm == DenoiserController::Algorithm::NRD)
            {
                passBuilder.ReadBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
                passBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
                passBuilder.ReadBuffer(DemoResourceIds::GBufferEmissionMetallic);
                passBuilder.ReadBuffer(DemoResourceIds::NRDNoisyRadiance);
                passBuilder.WriteUav(DemoResourceIds::NRDNormalRoughness);
                passBuilder.WriteUav(DemoResourceIds::NRDViewZ);
                passBuilder.WriteUav(DemoResourceIds::NRDMotion);
                passBuilder.WriteUav(DemoResourceIds::NRDDenoisedRadiance);
            }
            else
            {
                passBuilder.ReadBuffer(DemoResourceIds::NoisyRadiance);
            }
        },
        [](const DenoisePassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const DenoiserController::Algorithm algorithm = passData.Algorithm;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const uint32_t width = context.GetMetadata().m_ScreenWidth;
            const uint32_t height = context.GetMetadata().m_ScreenHeight;

            RaytracingDemoRenderGraph::LightingResources lighting = {};
            lighting.SceneColor = context.GetTexture(DemoResourceIds::SceneColor);
            if (algorithm == DenoiserController::Algorithm::NRD)
            {
                lighting.NRDNoisyRadiance = context.GetTexture(DemoResourceIds::NRDNoisyRadiance);
            }
            else
            {
                lighting.NoisyRadiance = context.GetTexture(DemoResourceIds::NoisyRadiance);
            }
            RaytracingDemoRenderGraph::NRDResources nrd = {};
            if (algorithm == DenoiserController::Algorithm::NRD)
            {
                nrd = RaytracingDemoRenderGraph::GetNRDResources(context);
            }
            const NRD::FrameMatrices frameMatrices = {
                resources.SceneCamera.GetViewMatrix(),
                resources.SceneCamera.GetProjectionMatrix(),
            };

            resources.Denoisers.Execute(
                cmd,
                frameMatrices,
                gbuffer,
                lighting,
                nrd,
                width,
                height);
        });
}
//Modify End
