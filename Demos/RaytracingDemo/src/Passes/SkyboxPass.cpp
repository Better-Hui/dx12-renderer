//Modify Begin:2026-08-24 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-08-23 by Hui
#include <Scene/SceneLightManager.h>
//Modify End

#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderGraphBuilder.h>

//Modify Begin:2026-08-23 by Hui
#include <algorithm>
#include <cmath>
//Modify End

using namespace DirectX;

namespace
{
    struct SkyboxPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };

//Modify Begin:2026-08-23 by Hui
    struct SkyboxSunConstants
    {
        XMFLOAT4 DirectionAndPadding = { 0.0f, 1.0f, 0.0f, 0.0f };
        XMFLOAT4 ColorAndIntensity = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    SkyboxSunConstants BuildSkyboxSunConstants(const RaytracingDemoPassResources& resources)
    {
        SkyboxSunConstants constants{};
        if (!resources.Lights.AreDirectionalLightsEnabled())
        {
            return constants;
        }

        const DirectionalLight* primaryLight = nullptr;
        float primaryLuminance = 0.0f;
        for (const DirectionalLight& light : resources.Lights.GetDirectionalLights())
        {
            const float luminance = std::max(
                0.0f,
                0.2126f * light.m_Color.x + 0.7152f * light.m_Color.y + 0.0722f * light.m_Color.z) *
                std::max(0.0f, light.m_Color.w);
            if (luminance > primaryLuminance)
            {
                primaryLight = &light;
                primaryLuminance = luminance;
            }
        }

        if (primaryLight == nullptr)
        {
            return constants;
        }

        const XMFLOAT4& direction = primaryLight->m_DirectionWs;
        const float directionLengthSquared =
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
        if (directionLengthSquared <= 1.0e-8f)
        {
            return constants;
        }

        const float inverseLength = 1.0f / std::sqrt(directionLengthSquared);
        constants.DirectionAndPadding = {
            direction.x * inverseLength,
            direction.y * inverseLength,
            direction.z * inverseLength,
            0.0f
        };
        constants.ColorAndIntensity = primaryLight->m_Color;
        return constants;
    }
//Modify End
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
//Modify Begin:2026-08-23 by Hui
            const SkyboxSunConstants sun = BuildSkyboxSunConstants(resources);
//Modify End
            CommandContext commandContext(cmd);

            commandContext.SetConstantBuffer(skyboxShader, "CameraConstants", sizeof(camera), &camera);
//Modify Begin:2026-08-23 by Hui
            commandContext.SetConstantBuffer(skyboxShader, "SkyboxSunConstants", sizeof(sun), &sun);
//Modify End
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
        });
}
//Modify End
