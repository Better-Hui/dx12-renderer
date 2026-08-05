//Modify Begin:2026-07-28 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateSkyboxPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::ResourceId sceneReadyToken)
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
        [resources, config](const RenderContext& context, CommandList& cmd)
        {
            if (config.SkyboxEnabled == nullptr || !*config.SkyboxEnabled)
            {
                return;
            }
//Modify Begin:2026-07-28 by BestHui
            const bool isEquirectangular =
                resources.SkyboxTexture->GetD3D12ResourceDesc().DepthOrArraySize == 1u;
            ComputeShader& skyboxShader = isEquirectangular
                ? *resources.SkyboxEquirectangularComputeShader
                : *resources.SkyboxComputeShader;
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            CommandContext commandContext(cmd);

            commandContext.SetConstantBuffer(skyboxShader, "CameraConstants", sizeof(camera), &camera);
            commandContext.SetTexture(skyboxShader, "DepthTexture", ShaderResourceView::DepthAsFloat(context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer)));
//Modify Begin:2026-07-30 by BestHui
            if (skyboxShader.HasShaderResourceView("SkyboxTexture"))
            {
                commandContext.SetTexture(
                    skyboxShader,
                    "SkyboxTexture",
                    isEquirectangular
                        ? ShaderResourceView(resources.SkyboxTexture)
                        : ShaderResourceView::TextureCube(resources.SkyboxTexture));
            }
//Modify End
            commandContext.SetUnorderedAccessView(skyboxShader, "SceneColor", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::SceneColor)));
            commandContext.BindPipeline(skyboxShader);
            commandContext.BindDescriptorSet(skyboxShader.GetDescriptorSet());
            commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            commandContext.InsertDescriptorSetOutputBarriers(skyboxShader.GetDescriptorSet());
//Modify End
        });
}
//Modify End
