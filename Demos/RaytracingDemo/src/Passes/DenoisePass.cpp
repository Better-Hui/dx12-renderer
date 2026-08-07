#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderPass.h>

//Modify Begin:2026-07-30 by BestHui
std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDenoisePass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig&)
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
        [resources](const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoRenderGraph::LightingResources lighting = RaytracingDemoRenderGraph::GetLightingResources(context);
            const uint32_t width = context.GetMetadata().m_ScreenWidth;
            const uint32_t height = context.GetMetadata().m_ScreenHeight;

            const RaytracingDemoRenderGraph::NRDResources nrd = RaytracingDemoRenderGraph::GetNRDResources(context);
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
