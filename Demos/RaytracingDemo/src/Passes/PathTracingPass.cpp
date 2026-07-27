#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/CommandContext.h>
#include <Framework/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDirectLightingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Direct Lighting",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::SkyboxFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::DirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::DirectLightingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (!demo.m_DirectLightingEnabled)
            {
                return;
            }

            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);

            demo.EnsureRayTracingPipelines();
            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, *demo.m_InlineDirectLightingShader, gbuffer, camera);
                demo.m_InlineDirectLightingShader->SetUnorderedAccessView(cmd, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                demo.m_InlineDirectLightingShader->ApplyBindings(cmd);
                CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                demo.m_DirectRayTracingBindingSet->SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, *demo.m_DirectRayTracingBindingSet, gbuffer, camera);
                CommandContext(cmd).DispatchRays(*demo.m_DirectRayTracingBindingSet, "DirectLightingRayGen", camera.Width, camera.Height);
            }
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateIndirectLightingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Indirect Lighting",
        {
            { DemoResourceIds::DirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::IndirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::IndirectLightingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (!demo.m_IndirectLightingEnabled)
            {
                return;
            }

            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);

            demo.EnsureRayTracingPipelines();
            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, *demo.m_InlineIndirectLightingShader, gbuffer, camera);
                demo.m_InlineIndirectLightingShader->SetUnorderedAccessView(cmd, "IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                demo.m_InlineIndirectLightingShader->ApplyBindings(cmd);
                CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                demo.m_IndirectRayTracingBindingSet->SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, *demo.m_IndirectRayTracingBindingSet, gbuffer, camera);
                CommandContext(cmd).DispatchRays(*demo.m_IndirectRayTracingBindingSet, "IndirectLightingRayGen", camera.Width, camera.Height);
            }
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateLightingCompositePass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Lighting Composite",
        {
            { DemoResourceIds::DirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::IndirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::DirectLighting, InputType::ShaderResource },
            { DemoResourceIds::IndirectLighting, InputType::ShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, OutputType::UnorderedAccess },
            { DemoResourceIds::Accumulation, OutputType::UnorderedAccess },
            { DemoResourceIds::NoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDNoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::RayTracingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);
            RaytracingDemoPassAccess::BindCompositeInputs(demo, cmd, context, gbuffer, camera);
            demo.m_LightingCompositeShader->ApplyBindings(cmd);
            CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
        });
}
