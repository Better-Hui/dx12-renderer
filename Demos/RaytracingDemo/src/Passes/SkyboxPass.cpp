//Modify Begin:2026-07-28 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

#include <Framework/CommandContext.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateSkyboxPass(RaytracingDemo& demo, const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Skybox",
        {
            { sceneReadyToken, InputType::Token },
//Modify Begin:2026-07-28 by BestHui
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
//Modify End
        },
        {
//Modify Begin:2026-07-28 by BestHui
            { DemoResourceIds::SceneColor, OutputType::UnorderedAccess },
//Modify End
            { DemoResourceIds::SkyboxFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
//Modify Begin:2026-07-28 by BestHui
            ComputeShader& skyboxShader = *demo.m_SkyboxComputeShader;
            const RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);

            cmd.SetConstantBuffer(skyboxShader, "CameraConstants", camera);
            cmd.SetTexture(skyboxShader, "DepthTexture", ShaderResourceView::DepthAsFloat(context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer)));
//Modify Begin:2026-07-30 by BestHui
            if (skyboxShader.HasShaderResourceView("SkyboxTexture"))
            {
                cmd.SetTexture(skyboxShader, "SkyboxTexture", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
            }
//Modify End
            cmd.SetUnorderedAccessView(skyboxShader, "SceneColor", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::SceneColor)));
            CommandContext commandContext(cmd);
            commandContext.BindPipeline(skyboxShader);
            commandContext.BindDescriptorSet(skyboxShader.GetDescriptorSet(), PipelineBindPoint::Compute);
            commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            commandContext.InsertDescriptorSetOutputBarriers(skyboxShader.GetDescriptorSet());
//Modify End
        });
}
//Modify End
