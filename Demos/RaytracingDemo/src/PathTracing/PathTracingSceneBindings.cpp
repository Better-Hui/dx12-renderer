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
    const uint32_t textureCount = static_cast<uint32_t>(demo.m_Textures.size());
    const std::vector<std::shared_ptr<Mesh>>& meshes = demo.m_RayTracingAccelerationStructure.GetMeshes();
    const uint32_t meshCount = static_cast<uint32_t>(meshes.size());

    Assert(textureCount <= demo.m_RayTracingSceneResourceLayout.TextureDescriptorCapacity, "Ray tracing texture descriptors exceed the scene descriptor table capacity.");
    Assert(meshCount <= demo.m_RayTracingSceneResourceLayout.GeometryDescriptorCapacity, "Ray tracing geometry descriptors exceed the scene descriptor table capacity.");

    shader.Bind(cmd);
    shader.SetConstantBuffer(cmd, "CameraConstants", camera);
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
        shader.SetShaderResourceView(cmd, "Materials", 0u, demo.m_MaterialBuffer);
    }
    if (shader.HasShaderResourceView("Geometries"))
    {
        shader.SetShaderResourceView(cmd, "Geometries", 0u, demo.m_GeometryBuffer);
    }
    demo.m_Lights.BindComputeResources(cmd, shader);

    if (shader.HasShaderResourceView("Textures"))
    {
        for (uint32_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
        {
            shader.SetShaderResourceView(cmd, "Textures", textureIndex, ShaderResourceView(demo.m_Textures[textureIndex]));
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
    shader.SetTexture("GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
    shader.SetTexture("GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
    shader.SetTexture("GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
    shader.SetTexture("GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
    shader.SetTexture("GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    shader.SetTexture("DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    demo.m_Lights.BindRayTracingResources(shader);
    shader.SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
}

void RaytracingDemoPassAccess::BindCompositeInputs(
    RaytracingDemo& demo,
    CommandList& cmd,
    const RenderGraph::RenderContext& context,
    const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
    const RaytracingDemo::CameraConstants& camera)
{
    const RaytracingDemoRenderGraph::LightingResources lighting = RaytracingDemoRenderGraph::GetLightingResources(context);

    demo.m_LightingCompositeShader->Bind(cmd);
    demo.m_LightingCompositeShader->SetConstantBuffer(cmd, "CameraConstants", camera);
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "DirectLightingTexture", ShaderResourceView(lighting.Direct));
    demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "IndirectLightingTexture", ShaderResourceView(lighting.Indirect));
    demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "Output", UnorderedAccessView(lighting.Output));
    demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "Accumulation", UnorderedAccessView(lighting.Accumulation));
    demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "NoisyRadiance", UnorderedAccessView(lighting.NoisyRadiance));
    demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "NRDNoisyRadiance", UnorderedAccessView(lighting.NRDNoisyRadiance));
}
