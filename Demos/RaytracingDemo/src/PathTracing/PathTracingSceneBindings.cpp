#include <PathTracing/PathTracingSceneBindings.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/ComputeShader.h>
#include <Framework/Mesh.h>
#include <Framework/RayTracingShader.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>
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
    const std::vector<std::shared_ptr<Texture>>& textures = demo.m_SceneResources.GetTextures();
    const uint32_t textureCount = static_cast<uint32_t>(textures.size());
    const std::vector<std::shared_ptr<Mesh>>& meshes = demo.m_RayTracingAccelerationStructure.GetMeshes();
    const uint32_t meshCount = static_cast<uint32_t>(meshes.size());

//Modify Begin:2026-07-27 by BestHui
    const RayTracingSceneResourceLayout& layout = demo.m_PathTracingPipelines.GetLayout();
    Assert(textureCount <= layout.TextureDescriptorCapacity, "Ray tracing texture descriptors exceed the scene descriptor table capacity.");
    Assert(meshCount <= layout.GeometryDescriptorCapacity, "Ray tracing geometry descriptors exceed the scene descriptor table capacity.");
//Modify End

    cmd.SetConstantBuffer(shader, "CameraConstants", camera);
    shader.SetAccelerationStructure(cmd, demo.m_RayTracingAccelerationStructure);
    if (shader.HasShaderResourceView("GBufferTextures"))
    {
        shader.SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
        shader.SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
        shader.SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
        shader.SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
        shader.SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    }
    if (shader.HasShaderResourceView("DepthTexture"))
    {
        shader.SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    }
    if (shader.HasShaderResourceView("Skybox"))
    {
        shader.SetShaderResourceView(cmd, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
    }
    if (shader.HasShaderResourceView("Materials"))
    {
        shader.SetShaderResourceView(cmd, "Materials", 0u, demo.m_SceneResources.GetMaterialBuffer());
    }
    if (shader.HasShaderResourceView("Geometries"))
    {
        shader.SetShaderResourceView(cmd, "Geometries", 0u, demo.m_SceneResources.GetGeometryBuffer());
    }
    demo.m_Lights.BindComputeResources(cmd, shader);

    if (shader.HasShaderResourceView("Textures"))
    {
        for (uint32_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
        {
            shader.SetShaderResourceView(cmd, "Textures", textureIndex, ShaderResourceView(textures[textureIndex]));
        }
    }

    if (shader.HasShaderResourceView("VertexBuffers"))
    {
        for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
        {
            const Mesh& mesh = *meshes[meshIndex];
            shader.SetShaderResourceView(cmd, "VertexBuffers", meshIndex, mesh.GetVertexBuffer());
        }
    }

    if (shader.HasShaderResourceView("IndexBuffers"))
    {
        for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
        {
            const Mesh& mesh = *meshes[meshIndex];
            shader.SetShaderResourceView(cmd, "IndexBuffers", meshIndex, mesh.GetIndexBuffer());
        }
    }
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

//Modify Begin:2026-07-27 by BestHui
    ComputeShader& compositeShader = demo.m_PathTracingPipelines.GetLightingCompositeShader();
    cmd.SetConstantBuffer(compositeShader, "CameraConstants", camera);
    compositeShader.SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
    compositeShader.SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
    compositeShader.SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
    compositeShader.SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
    compositeShader.SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    compositeShader.SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    compositeShader.SetShaderResourceView(cmd, "DirectLightingTexture", ShaderResourceView(lighting.Direct));
    compositeShader.SetShaderResourceView(cmd, "IndirectLightingTexture", ShaderResourceView(lighting.Indirect));
    cmd.SetUnorderedAccessView(compositeShader, "SceneColor", UnorderedAccessView(lighting.SceneColor));
    cmd.SetUnorderedAccessView(compositeShader, "HistoryColor", UnorderedAccessView(lighting.HistoryColor));
    cmd.SetUnorderedAccessView(compositeShader, "NoisyRadiance", UnorderedAccessView(lighting.NoisyRadiance));
    cmd.SetUnorderedAccessView(compositeShader, "NRDNoisyRadiance", UnorderedAccessView(lighting.NRDNoisyRadiance));
//Modify End
}
