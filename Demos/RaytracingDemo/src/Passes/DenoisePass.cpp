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
            { DemoResourceIds::NoisyRadiance, InputType::ShaderResource },
            { DemoResourceIds::NRDNoisyRadiance, InputType::ShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::MotionVector, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
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

            demo.GetDenoisers().Execute(
                cmd,
                frameMatrices,
                gbuffer,
                lighting,
                nrd,
                width,
                height);
        });
}
