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

#include <vector>

RaytracingDemo::CameraConstants RaytracingDemoPassAccess::BuildPassCameraConstants(
    RaytracingDemo& demo,
    const RenderGraph::RenderContext& context)
{
    RaytracingDemo::CameraConstants camera = demo.BuildCameraConstants();
    camera.Width = context.m_Metadata.m_ScreenWidth;
    camera.Height = context.m_Metadata.m_ScreenHeight;
    camera.FrameIndex = static_cast<uint32_t>(context.m_Metadata.m_FrameIndex);
    return camera;
}

void RaytracingDemoPassAccess::BindInlinePathTracingInputs(
    RaytracingDemo& demo,
    CommandList& cmd,
    ComputeShader& shader,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemo::CameraConstants& camera)
{
    const RayTracingAccelerationStructure& accelerationStructure = demo.m_SceneResources.GetRayTracingAccelerationStructure();
    CommandContext commandContext(cmd);

    commandContext.SetConstantBuffer(shader, "CameraConstants", sizeof(camera), &camera);
    commandContext.SetAccelerationStructure(shader, accelerationStructure);
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
    if (shader.HasShaderResourceView("Skybox"))
    {
        commandContext.SetShaderResourceView(shader, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
    }
    if (shader.HasShaderResourceView("Materials"))
    {
        commandContext.SetShaderResource(shader, "Materials", 0u, demo.m_SceneResources.GetMaterialBuffer());
    }
    if (shader.HasShaderResourceView("Geometries"))
    {
        commandContext.SetShaderResource(shader, "Geometries", 0u, demo.m_SceneResources.GetGeometryBuffer());
    }
//Modify Begin:2026-07-30 by BestHui
    if (shader.HasShaderResourceView("BindlessTextures"))
    {
        const std::vector<ShaderResourceView> sceneTextures = demo.m_SceneResources.CreateTextureShaderResourceViews();
        commandContext.SetShaderResourceViews(shader, "BindlessTextures", sceneTextures);
    }
//Modify End
    demo.m_Lights.BindComputeResources(commandContext, shader);
}

void RaytracingDemoPassAccess::BindDxrPathTracingInputs(
    RaytracingDemo& demo,
    RayTracingBindingSet& shader,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemo::CameraConstants& camera)
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
//Modify Begin:2026-07-30 by BestHui
    if (shader.HasBinding("BindlessTextures"))
    {
        shader.SetTextureArray("BindlessTextures", demo.m_SceneResources.CreateTextureShaderResourceViews());
    }
//Modify End
    demo.m_Lights.BindRayTracingResources(shader);
    if (shader.HasBinding("CameraConstants"))
    {
        shader.SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
    }
}

void RaytracingDemoPassAccess::BindCompositeInputs(
    RaytracingDemo& demo,
    CommandList& cmd,
    const RenderGraph::RenderContext& context,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemo::CameraConstants& camera)
{
    const RaytracingDemoRenderGraph::LightingResources lighting = RaytracingDemoRenderGraph::GetLightingResources(context);
    CommandContext commandContext(cmd);

//Modify Begin:2026-07-27 by BestHui
    ComputeShader& compositeShader = demo.m_PathTracingPipelines.GetLightingCompositeShader();
    commandContext.SetConstantBuffer(compositeShader, "CameraConstants", sizeof(camera), &camera);
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
    commandContext.SetShaderResourceView(compositeShader, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    commandContext.SetShaderResourceView(compositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    commandContext.SetShaderResourceView(compositeShader, "DirectLightingTexture", ShaderResourceView(lighting.Direct));
    commandContext.SetShaderResourceView(compositeShader, "IndirectLightingTexture", ShaderResourceView(lighting.Indirect));
    commandContext.SetUnorderedAccessView(compositeShader, "SceneColor", UnorderedAccessView(lighting.SceneColor));
    commandContext.SetUnorderedAccessView(compositeShader, "HistoryColor", UnorderedAccessView(lighting.HistoryColor));
    commandContext.SetUnorderedAccessView(compositeShader, "NoisyRadiance", UnorderedAccessView(lighting.NoisyRadiance));
    commandContext.SetUnorderedAccessView(compositeShader, "NRDNoisyRadiance", UnorderedAccessView(lighting.NRDNoisyRadiance));
//Modify End
}
