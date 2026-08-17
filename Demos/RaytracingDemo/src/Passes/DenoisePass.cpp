#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderPass.h>

#include <vector>

//Modify Begin:2026-07-30 by Hui
std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDenoisePass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    const DenoiserController::Algorithm algorithm = config.FrameState->DenoiserAlgorithm;
    Assert(algorithm != DenoiserController::Algorithm::Off, "Denoise pass requires an active algorithm.");

    std::vector<Input> inputs = {
        { DemoResourceIds::RayTracingFinishedToken, InputType::Token },
        { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
        { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
        { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
        { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
    };
    std::vector<Output> outputs = {
        { DemoResourceIds::SceneColor, OutputType::UnorderedAccess },
        { DemoResourceIds::DenoiseFinishedToken, OutputType::Token },
    };
    if (algorithm == DenoiserController::Algorithm::NRD)
    {
        inputs.emplace_back(DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource);
        inputs.emplace_back(DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource);
        inputs.emplace_back(DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource);
        inputs.emplace_back(DemoResourceIds::NRDNoisyRadiance, InputType::NonPixelShaderResource);
        outputs.emplace_back(DemoResourceIds::NRDNormalRoughness, OutputType::UnorderedAccess);
        outputs.emplace_back(DemoResourceIds::NRDViewZ, OutputType::UnorderedAccess);
        outputs.emplace_back(DemoResourceIds::NRDMotion, OutputType::UnorderedAccess);
        outputs.emplace_back(DemoResourceIds::NRDDenoisedRadiance, OutputType::UnorderedAccess);
    }
    else
    {
        inputs.emplace_back(DemoResourceIds::NoisyRadiance, InputType::NonPixelShaderResource);
    }

    return RenderPass::Create(
        L"Denoise",
        inputs,
        outputs,
        [resources, algorithm](const RenderContext& context, CommandList& cmd)
        {
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
