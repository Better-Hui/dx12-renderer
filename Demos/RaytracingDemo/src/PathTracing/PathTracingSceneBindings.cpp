#include <PathTracing/PathTracingSceneBindings.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>
//Modify Begin:2026-07-30 by BestHui
#include <Scene/SceneLightManager.h>
//Modify End

#include <vector>

//Modify Begin:2026-07-30 by BestHui
RaytracingDemoCameraConstants RaytracingDemoPassBindings::BuildPassCameraConstants(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::RenderContext& context)
{
    return ::BuildPassCameraConstants(resources, config, context);
}

void RaytracingDemoPassBindings::BindInlinePathTracingInputs(
    const RaytracingDemoPassResources& resources,
    CommandContext& commandContext,
    ComputeShader& shader,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemoCameraConstants& camera)
{
    const RayTracingAccelerationStructure& accelerationStructure = resources.Scene.GetRayTracingAccelerationStructure();

    if (shader.HasConstantBuffer("CameraConstants"))
    {
        commandContext.SetConstantBuffer(shader, "CameraConstants", sizeof(camera), &camera);
    }
//Modify Begin:2026-07-30 by BestHui
    if (shader.HasAccelerationStructure("g_InlineRayTracingScene"))
    {
        commandContext.SetAccelerationStructure(shader, "g_InlineRayTracingScene", accelerationStructure);
    }
//Modify End
    if (shader.HasShaderResourceView("GBufferTextures"))
    {
        commandContext.SetShaderResourceView(shader, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
        commandContext.SetShaderResourceView(shader, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
        commandContext.SetShaderResourceView(shader, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
        commandContext.SetShaderResourceView(shader, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
        commandContext.SetShaderResourceView(shader, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    }
    if (shader.HasShaderResourceView("DepthTexture"))
    {
        commandContext.SetShaderResourceView(shader, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    }
//Modify Begin:2026-08-05 by BestHui
    if (shader.HasShaderResourceView("MotionVectorTexture"))
    {
        commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(gbuffer.MotionVector));
    }
//Modify End
    if (shader.HasShaderResourceView("Skybox"))
    {
        commandContext.SetShaderResourceView(shader, "Skybox", ShaderResourceView::EnvironmentTexture(resources.SkyboxTexture));
    }
    if (shader.HasShaderResourceView("Materials"))
    {
        commandContext.SetShaderResource(shader, "Materials", 0u, resources.Scene.GetMaterialBuffer());
    }
    if (shader.HasShaderResourceView("Geometries"))
    {
        commandContext.SetShaderResource(shader, "Geometries", 0u, resources.Scene.GetGeometryBuffer());
    }
    if (shader.HasShaderResourceView("BindlessTextures"))
    {
//Modify Begin:2026-08-11 by BestHui
        const std::vector<ShaderResourceView>& sceneTextures = resources.Scene.GetTextureShaderResourceViews();
//Modify End
        commandContext.SetShaderResourceViews(shader, "BindlessTextures", sceneTextures);
    }
    resources.Lights.BindComputeResources(commandContext, shader);
}

void RaytracingDemoPassBindings::BindDxrPathTracingInputs(
    const RaytracingDemoPassResources& resources,
    RayTracingBindingSet& shader,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemoCameraConstants& camera)
{
    if (shader.HasBinding("GBufferTextures"))
    {
        shader.SetTexture("GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
        shader.SetTexture("GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
        shader.SetTexture("GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
        shader.SetTexture("GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
        shader.SetTexture("GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    }
    if (shader.HasBinding("DepthTexture"))
    {
        shader.SetTexture("DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    }
    if (shader.HasBinding("BindlessTextures"))
    {
//Modify Begin:2026-08-11 by BestHui
        shader.SetTextureArray("BindlessTextures", resources.Scene.GetTextureShaderResourceViews());
//Modify End
    }
    resources.Lights.BindRayTracingResources(shader);
    if (shader.HasBinding("CameraConstants"))
    {
        shader.SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
    }
}

ComputeShader& RaytracingDemoPassBindings::BindCompositeInputs(
    const RaytracingDemoPassResources& resources,
    CommandList& cmd,
    const RenderGraph::RenderContext& context,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemoCameraConstants& camera)
{
    const RaytracingDemoRenderGraph::LightingResources lighting = RaytracingDemoRenderGraph::GetLightingResources(context);
    CommandContext commandContext(cmd);

    const PathTracingCompositeFeatures features {
        .DirectLightingEnabled = camera.DirectLightingActive != 0u,
        .IndirectLightingEnabled = camera.IndirectLightingActive != 0u,
        .AccumulationEnabled = camera.AccumulationEnabled != 0u,
        .DenoiserMode = camera.DenoiserEnabled,
        .UseNrdReblur = camera.NRDDenoiserMode == 1u,
    };
    ComputeShader& compositeShader = resources.Pipelines.GetLightingCompositeShader(features);
    commandContext.SetConstantBuffer(compositeShader, "CameraConstants", sizeof(camera), &camera);
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    commandContext.SetShaderResourceView(compositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    if (compositeShader.HasShaderResourceView("DirectLightingTexture"))
    {
        commandContext.SetShaderResourceView(compositeShader, "DirectLightingTexture", ShaderResourceView(lighting.Direct));
    }
    if (compositeShader.HasShaderResourceView("IndirectLightingTexture"))
    {
        commandContext.SetShaderResourceView(compositeShader, "IndirectLightingTexture", ShaderResourceView(lighting.Indirect));
    }
    commandContext.SetUnorderedAccessView(compositeShader, "SceneColor", UnorderedAccessView(lighting.SceneColor));
    if (compositeShader.HasUnorderedAccessView("HistoryColor"))
    {
        commandContext.SetUnorderedAccessView(compositeShader, "HistoryColor", UnorderedAccessView(lighting.HistoryColor));
    }
    if (compositeShader.HasUnorderedAccessView("NoisyRadiance"))
    {
        commandContext.SetUnorderedAccessView(compositeShader, "NoisyRadiance", UnorderedAccessView(lighting.NoisyRadiance));
    }
    if (compositeShader.HasUnorderedAccessView("NRDNoisyRadiance"))
    {
        commandContext.SetUnorderedAccessView(compositeShader, "NRDNoisyRadiance", UnorderedAccessView(lighting.NRDNoisyRadiance));
    }
    return compositeShader;
}
//Modify End
