//Modify Begin:2026-08-18 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderGraphBuilder.h>

using namespace DirectX;

namespace
{
    struct SkyboxPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };
}

void RaytracingDemoPasses::Builder::AddSkyboxPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<SkyboxPassData>(
        L"Skybox",
        [&resources, config, sceneReadyToken](RenderGraphPassBuilder& passBuilder, SkyboxPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadTexture(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::SceneColor);
            passBuilder.WriteToken(DemoResourceIds::SkyboxFinishedToken);
        },
        [](const SkyboxPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            if (!config.FrameState->SkyboxEnabled)
            {
                return;
            }
            const EnvironmentTextureProjection projection =
                ShaderResourceView::GetEnvironmentTextureProjection(*resources.SkyboxTexture);
            ComputeShader& skyboxShader = projection == EnvironmentTextureProjection::Equirectangular
                ? *resources.SkyboxEquirectangularComputeShader
                : projection == EnvironmentTextureProjection::CubemapHorizontalStrip
                    ? *resources.SkyboxCubemapStripComputeShader
                    : *resources.SkyboxComputeShader;
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            CommandContext commandContext(cmd);

            commandContext.SetConstantBuffer(skyboxShader, "CameraConstants", sizeof(camera), &camera);
            commandContext.SetTexture(skyboxShader, "DepthTexture", ShaderResourceView::DepthAsFloat(context.GetTexture(DemoResourceIds::DepthBuffer)));
            if (skyboxShader.HasShaderResourceView("SkyboxTexture"))
            {
                commandContext.SetTexture(
                    skyboxShader,
                    "SkyboxTexture",
                    ShaderResourceView::EnvironmentTexture(resources.SkyboxTexture));
            }
            commandContext.SetUnorderedAccessView(skyboxShader, "SceneColor", UnorderedAccessView(context.GetTexture(DemoResourceIds::SceneColor)));
            commandContext.BindPipeline(skyboxShader);
            commandContext.BindDescriptorSet(skyboxShader.GetDescriptorSet());
            commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            commandContext.InsertDescriptorSetOutputBarriers(skyboxShader.GetDescriptorSet());
        });
}
//Modify End
