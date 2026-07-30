#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
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
//Modify Begin:2026-07-30 by BestHui
            const bool useMeshletGBuffer = demo.m_UseMeshletGBuffer && demo.m_SceneResources.HasMeshlets();
            RaytracingDemo::GBufferDebugConstants debugConstants{};
            debugConstants.DebugMeshletClusters = (useMeshletGBuffer && demo.m_DebugMeshletClusters) ? 1u : 0u;
//Modify Begin:2026-07-30 by BestHui
            const std::vector<ShaderResourceView> sceneTextures = demo.m_SceneResources.CreateTextureShaderResourceViews();
//Modify End
            if (useMeshletGBuffer)
            {
                commandContext.BindPipeline(*demo.m_GBufferMeshShader);
//Modify Begin:2026-07-30 by BestHui
                commandContext.BindBindlessDescriptorHeap(demo.m_SceneResources.GetBindlessDescriptorHeap());
//Modify End
//Modify Begin:2026-07-30 by BestHui
                if (demo.m_GBufferMeshShader->GetDescriptorSet().HasBinding("SceneTextures", DescriptorBindingKind::ShaderResourceView))
                {
                    demo.m_GBufferMeshShader->SetShaderResourceViews(cmd, "SceneTextures", sceneTextures);
                }
//Modify End
                demo.m_GBufferMeshShader->SetStructuredBuffer(cmd, "MeshletVertices", demo.m_SceneResources.GetMeshletVertexBuffer());
                demo.m_GBufferMeshShader->SetShaderResource(cmd, "MeshletIndices", demo.m_SceneResources.GetMeshletIndexBuffer());
                demo.m_GBufferMeshShader->SetStructuredBuffer(cmd, "Meshlets", demo.m_SceneResources.GetMeshletBuffer());
                cmd.SetConstantBuffer(demo.m_GBufferMeshShader, "GBufferDebugCBuffer", debugConstants);

                const XMMATRIX viewProjection = demo.GetSceneCamera().GetViewMatrix() * demo.GetSceneCamera().GetProjectionMatrix();
                const XMMATRIX previousViewProjection = demo.m_HasPreviousViewProjection ? demo.m_PreviousViewProjection : viewProjection;
                const auto& materials = demo.m_SceneResources.GetMaterials();
                for (const RaytracingDemoMeshletDraw& draw : demo.m_SceneResources.GetMeshletDraws())
                {
                    const RaytracingDemo::MaterialData& material = materials[draw.MaterialIndex];

                    RaytracingDemo::MeshletGBufferDrawConstants drawConstants{};
                    drawConstants.Model = draw.WorldMatrix;
                    drawConstants.ModelViewProjection = draw.WorldMatrix * viewProjection;
                    drawConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, draw.WorldMatrix));
                    drawConstants.PreviousModelViewProjection = draw.WorldMatrix * previousViewProjection;
                    drawConstants.MeshletOffset = draw.MeshletOffset;
                    drawConstants.MeshletCount = draw.MeshletCount;
                    cmd.SetConstantBuffer(demo.m_GBufferMeshShader, "MeshletDrawCBuffer", drawConstants);

                    RaytracingDemo::GBufferMaterialConstants materialConstants{};
                    materialConstants.Diffuse = material.Diffuse;
                    materialConstants.Specular = material.Specular;
                    materialConstants.TilingOffset = material.TilingOffset;
//Modify Begin:2026-07-30 by BestHui
                    materialConstants.DiffuseTextureIndex = material.DiffuseTextureIndex;
                    materialConstants.NormalTextureIndex = material.NormalTextureIndex;
                    materialConstants.MetallicTextureIndex = material.MetallicTextureIndex;
                    materialConstants.RoughnessTextureIndex = material.RoughnessTextureIndex;
                    materialConstants.AmbientOcclusionTextureIndex = material.AmbientOcclusionTextureIndex;
//Modify End
                    materialConstants.Metallic = material.Metallic;
                    materialConstants.Roughness = material.Roughness;
                    materialConstants.HasDiffuseMap = material.HasDiffuseMap;
                    materialConstants.HasNormalMap = material.HasNormalMap;
                    materialConstants.HasMetallicMap = material.HasMetallicMap;
                    materialConstants.HasRoughnessMap = material.HasRoughnessMap;
                    materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
                    cmd.SetConstantBuffer(demo.m_GBufferMeshShader, "MaterialCBuffer", materialConstants);
                    commandContext.BindDescriptorSet(demo.m_GBufferMeshShader->GetDescriptorSet());
                    commandContext.DispatchMesh(draw.MeshletCount, 1, 1);
                }
                return;
            }
//Modify End
            commandContext.BindPipeline(*demo.m_GBufferShader);
//Modify Begin:2026-07-30 by BestHui
            commandContext.BindBindlessDescriptorHeap(demo.m_SceneResources.GetBindlessDescriptorHeap());
//Modify End
//Modify Begin:2026-07-30 by BestHui
            if (demo.m_GBufferShader->HasShaderResourceView("SceneTextures"))
            {
                demo.m_GBufferShader->SetShaderResourceViews(cmd, "SceneTextures", sceneTextures);
            }
//Modify End
            cmd.SetConstantBuffer(demo.m_GBufferShader, "GBufferDebugCBuffer", debugConstants);
//Modify End

            const XMMATRIX viewProjection = demo.GetSceneCamera().GetViewMatrix() * demo.GetSceneCamera().GetProjectionMatrix();
            const XMMATRIX previousViewProjection = demo.m_HasPreviousViewProjection ? demo.m_PreviousViewProjection : viewProjection;
            const auto& sceneObjects = demo.m_SceneResources.GetSceneObjects();
            const auto& materials = demo.m_SceneResources.GetMaterials();
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
//Modify Begin:2026-07-30 by BestHui
                materialConstants.DiffuseTextureIndex = material.DiffuseTextureIndex;
                materialConstants.NormalTextureIndex = material.NormalTextureIndex;
                materialConstants.MetallicTextureIndex = material.MetallicTextureIndex;
                materialConstants.RoughnessTextureIndex = material.RoughnessTextureIndex;
                materialConstants.AmbientOcclusionTextureIndex = material.AmbientOcclusionTextureIndex;
//Modify End
                materialConstants.Metallic = material.Metallic;
                materialConstants.Roughness = material.Roughness;
                materialConstants.HasDiffuseMap = material.HasDiffuseMap;
                materialConstants.HasNormalMap = material.HasNormalMap;
                materialConstants.HasMetallicMap = material.HasMetallicMap;
                materialConstants.HasRoughnessMap = material.HasRoughnessMap;
                materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
                cmd.SetConstantBuffer(demo.m_GBufferShader, "MaterialCBuffer", materialConstants);

//Modify Begin:2026-07-29 by BestHui
                commandContext.BindDescriptorSet(demo.m_GBufferShader->GetDescriptorSet());
//Modify End
//Modify Begin:2026-07-30 by BestHui
                for (const auto& mesh : object.Model->GetMeshes())
                {
                    mesh->Bind(cmd);
                    commandContext.DrawIndexed(mesh->GetIndexCount());
                }
//Modify End
            }
        });
}
