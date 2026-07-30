#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Mesh.h>
#include <Framework/CommandContext.h>
#include <Framework/ShaderResourceView.h>
#include <RenderGraph/RenderPass.h>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateBaseResourcesPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Base Resources",
        {},
        {
            { DemoResourceIds::GBufferAlbedoOcclusion, OutputType::RenderTarget },
            { DemoResourceIds::GBufferSpecularSmoothness, OutputType::RenderTarget },
            { DemoResourceIds::GBufferNormal, OutputType::RenderTarget },
            { DemoResourceIds::GBufferEmissionMetallic, OutputType::RenderTarget },
            { DemoResourceIds::GBufferPosition, OutputType::RenderTarget },
            { DemoResourceIds::MotionVector, OutputType::RenderTarget },
            { DemoResourceIds::DepthBuffer, OutputType::DepthWrite },
            { DemoResourceIds::BaseResourcesFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
//Modify Begin:2026-07-29 by BestHui
//Modify Begin:2026-07-30 by BestHui
            if (demo.m_Lights.Upload(cmd))
            {
                demo.BindRayTracingShaderResources();
            }
//Modify End
            CommandContext commandContext(cmd);
            commandContext.BindPipeline(*demo.m_GBufferShader);
//Modify End

            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            const XMMATRIX previousViewProjection = demo.m_HasPreviousViewProjection ? demo.m_PreviousViewProjection : viewProjection;
            const auto& sceneObjects = demo.m_SceneResources.GetSceneObjects();
            const auto& materials = demo.m_SceneResources.GetMaterials();
            const auto& textures = demo.m_SceneResources.GetTextures();
            for (const RaytracingDemo::SceneObject& object : sceneObjects)
            {
                const RaytracingDemo::MaterialData& material = materials[object.MaterialIndex];

                RaytracingDemo::ModelConstants modelConstants{};
                modelConstants.Model = object.WorldMatrix;
                modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
                modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
                modelConstants.PreviousModelViewProjection = object.WorldMatrix * previousViewProjection;
                cmd.SetConstantBuffer(demo.m_GBufferShader, "ModelCBuffer", modelConstants);

                RaytracingDemo::GBufferMaterialConstants materialConstants{};
                materialConstants.Diffuse = material.Diffuse;
                materialConstants.Specular = material.Specular;
                materialConstants.TilingOffset = material.TilingOffset;
                materialConstants.Metallic = material.Metallic;
                materialConstants.Roughness = material.Roughness;
                materialConstants.HasDiffuseMap = material.HasDiffuseMap;
                materialConstants.HasNormalMap = material.HasNormalMap;
                materialConstants.HasMetallicMap = material.HasMetallicMap;
                materialConstants.HasRoughnessMap = material.HasRoughnessMap;
                materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
                cmd.SetConstantBuffer(demo.m_GBufferShader, "MaterialCBuffer", materialConstants);
                cmd.SetTexture(demo.m_GBufferShader, "DiffuseTexture", ShaderResourceView(textures[material.DiffuseTextureIndex]));
                cmd.SetTexture(demo.m_GBufferShader, "NormalTexture", ShaderResourceView(textures[material.NormalTextureIndex]));
                cmd.SetTexture(demo.m_GBufferShader, "MetallicTexture", ShaderResourceView(textures[material.MetallicTextureIndex]));
                cmd.SetTexture(demo.m_GBufferShader, "RoughnessTexture", ShaderResourceView(textures[material.RoughnessTextureIndex]));
                cmd.SetTexture(demo.m_GBufferShader, "AmbientOcclusionTexture", ShaderResourceView(textures[material.AmbientOcclusionTextureIndex]));

//Modify Begin:2026-07-29 by BestHui
                commandContext.BindDescriptorSet(demo.m_GBufferShader->GetDescriptorSet(), PipelineBindPoint::Graphics);
//Modify End
                object.Model->Draw(cmd);
            }
        });
}
