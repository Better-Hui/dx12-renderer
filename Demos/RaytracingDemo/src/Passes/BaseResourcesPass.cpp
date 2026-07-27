#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Mesh.h>
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
            demo.m_GBufferShader->Bind(cmd);

            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            const XMMATRIX previousViewProjection = demo.m_HasPreviousViewProjection ? demo.m_PreviousViewProjection : viewProjection;
            for (const RaytracingDemo::SceneObject& object : demo.m_SceneObjects)
            {
                const RaytracingDemo::MaterialData& material = demo.m_Materials[object.MaterialIndex];

                RaytracingDemo::ModelConstants modelConstants{};
                modelConstants.Model = object.WorldMatrix;
                modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
                modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
                modelConstants.PreviousModelViewProjection = object.WorldMatrix * previousViewProjection;
                demo.m_GBufferShader->SetConstantBuffer(cmd, "ModelCBuffer", modelConstants);

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
                demo.m_GBufferShader->SetConstantBuffer(cmd, "MaterialCBuffer", materialConstants);
                demo.m_GBufferShader->SetTexture(cmd, "DiffuseTexture", ShaderResourceView(demo.m_Textures[material.DiffuseTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "NormalTexture", ShaderResourceView(demo.m_Textures[material.NormalTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "MetallicTexture", ShaderResourceView(demo.m_Textures[material.MetallicTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "RoughnessTexture", ShaderResourceView(demo.m_Textures[material.RoughnessTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "AmbientOcclusionTexture", ShaderResourceView(demo.m_Textures[material.AmbientOcclusionTextureIndex]));

                demo.m_GBufferShader->ApplyBindings(cmd);
                object.Model->Draw(cmd);
            }

            demo.m_GBufferShader->Unbind(cmd);
        });
}
