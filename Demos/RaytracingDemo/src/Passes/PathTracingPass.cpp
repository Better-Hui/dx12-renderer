#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/CommandContext.h>
#include <Framework/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

#include <string_view>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

//Modify Begin:2026-07-27 by BestHui
    void DispatchDxrLightingPass(
        CommandList& cmd,
        RayTracingBindingSet& bindingSet,
        const std::string_view passName,
        const uint32_t width,
        const uint32_t height)
    {
        const D3D12_DISPATCH_RAYS_DESC dispatchDesc = bindingSet.GetShader().BuildDispatchDesc(passName, width, height, 1u);
        CommandContext commandContext(cmd);
        commandContext.BindRayTracingDescriptorSet(bindingSet);
        commandContext.DispatchRays(dispatchDesc);
        commandContext.InsertDescriptorSetOutputBarriers(bindingSet.GetDescriptorSet());
    }
//Modify End
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDirectLightingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Direct Lighting",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
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
            if (demo.m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& directLightingShader = demo.m_PathTracingPipelines.GetInlineDirectLightingShader();
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, directLightingShader, gbuffer, camera);
                directLightingShader.SetUnorderedAccessView(cmd, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                directLightingShader.ApplyBindings(cmd);
                CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                RayTracingBindingSet& directBindingSet = demo.m_PathTracingPipelines.GetDirectRayTracingBindingSet();
                directBindingSet.SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, directBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by BestHui
                DispatchDxrLightingPass(cmd, directBindingSet, "DirectLightingRayGen", camera.Width, camera.Height);
//Modify End
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
            if (demo.m_PathTracingBackend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& indirectLightingShader = demo.m_PathTracingPipelines.GetInlineIndirectLightingShader();
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, indirectLightingShader, gbuffer, camera);
                indirectLightingShader.SetUnorderedAccessView(cmd, "IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                indirectLightingShader.ApplyBindings(cmd);
                CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                RayTracingBindingSet& indirectBindingSet = demo.m_PathTracingPipelines.GetIndirectRayTracingBindingSet();
                indirectBindingSet.SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, indirectBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by BestHui
                DispatchDxrLightingPass(cmd, indirectBindingSet, "IndirectLightingRayGen", camera.Width, camera.Height);
//Modify End
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
            { DemoResourceIds::SceneColor, OutputType::UnorderedAccess },
            { DemoResourceIds::HistoryColor, OutputType::UnorderedAccess },
            { DemoResourceIds::NoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDNoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::RayTracingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);
            RaytracingDemoPassAccess::BindCompositeInputs(demo, cmd, context, gbuffer, camera);
            demo.m_PathTracingPipelines.GetLightingCompositeShader().ApplyBindings(cmd);
            CommandContext(cmd).Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
        });
}
