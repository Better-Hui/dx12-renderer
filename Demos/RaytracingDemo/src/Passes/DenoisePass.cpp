#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDenoisePass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Denoise",
        {
            { DemoResourceIds::RayTracingFinishedToken, InputType::Token },
//Modify Begin:2026-07-30 by BestHui
            { DemoResourceIds::NoisyRadiance, InputType::NonPixelShaderResource },
            { DemoResourceIds::NRDNoisyRadiance, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
//Modify End
        },
        {
            { DemoResourceIds::NRDNormalRoughness, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDViewZ, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDMotion, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDDenoisedRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::SceneColor, OutputType::UnorderedAccess },
            { DemoResourceIds::DenoiseFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoRenderGraph::LightingResources lighting = RaytracingDemoRenderGraph::GetLightingResources(context);
            const uint32_t width = context.m_Metadata.m_ScreenWidth;
            const uint32_t height = context.m_Metadata.m_ScreenHeight;

            const RaytracingDemoRenderGraph::NRDResources nrd = RaytracingDemoRenderGraph::GetNRDResources(context);
            const NRD::FrameMatrices frameMatrices = {
                demo.GetSceneCamera().GetViewMatrix(),
                demo.GetSceneCamera().GetProjectionMatrix(),
            };

//Modify Begin:2026-07-30 by BestHui
            const NRD::ResourceTransitionCallback transitionResource =
                [&context](
                    CommandList& transitionCommandList,
                    const std::span<const ResourceStateTransition> transitions)
                {
                    context.TransitionResources(transitionCommandList, transitions);
                };
//Modify End

            demo.GetDenoisers().Execute(
                cmd,
                frameMatrices,
                gbuffer,
                lighting,
                nrd,
                width,
                height,
                transitionResource);
        });
}
