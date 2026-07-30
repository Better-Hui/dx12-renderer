#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
//Modify End
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
                constexpr uint32_t MeshletCullThreadCount = 64;
                const uint32_t meshletInstanceCount = demo.m_SceneResources.GetMeshletInstanceCount();
                uint32_t clearCounter = 0;
                cmd.CopyByteAddressBuffer(demo.m_SceneResources.GetMeshletIndirectCommandBuffer().GetCounterBuffer(), clearCounter);
                cmd.TransitionBarrier(
                    demo.m_SceneResources.GetMeshletIndirectCommandBuffer().GetCounterBuffer(),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                struct MeshletCullConstants
                {
                    DirectX::XMFLOAT4 FrustumPlanes[6] = {};
                    uint32_t InstanceCount = 0;
                    uint32_t DebugDisableCulling = 0;
                    uint32_t Padding0 = 0;
                    uint32_t Padding1 = 0;
                } cullConstants;

                const Camera::Frustum frustum = demo.GetSceneCamera().GetFrustum();
                for (uint32_t planeIndex = 0; planeIndex < Camera::Frustum::PLANES_COUNT; ++planeIndex)
                {
                    const Camera::FrustumPlane& plane = frustum.m_Planes[planeIndex];
                    cullConstants.FrustumPlanes[planeIndex] = {
                        plane.m_Normal.x,
                        plane.m_Normal.y,
                        plane.m_Normal.z,
                        plane.m_Distance
                    };
                }
                cullConstants.InstanceCount = meshletInstanceCount;
                cullConstants.DebugDisableCulling = demo.m_DebugMeshletClusters ? 1u : 0u;

                ComputeShader& cullShader = *demo.m_MeshletCullShader;
                commandContext.BindPipeline(cullShader);
                cullShader.SetStructuredBuffer(cmd, "Meshlets", demo.m_SceneResources.GetMeshletBuffer());
                cullShader.SetStructuredBuffer(cmd, "MeshletInstances", demo.m_SceneResources.GetMeshletInstanceBuffer());
                cullShader.SetStructuredBuffer(cmd, "MeshletTransforms", demo.m_SceneResources.GetMeshletTransformBuffer());
                cullShader.SetUnorderedAccessView(
                    cmd,
                    "MeshletIndirectCommands",
                    UnorderedAccessView(demo.m_SceneResources.GetMeshletIndirectCommandBuffer()));
                cullShader.SetUnorderedAccessView(
                    cmd,
                    "MeshletIndirectCount",
                    UnorderedAccessView(demo.m_SceneResources.GetMeshletIndirectCommandBuffer().GetCounterBuffer()));
                cmd.SetConstantBuffer(demo.m_MeshletCullShader, "MeshletCullCBuffer", cullConstants);
                commandContext.BindDescriptorSet(cullShader.GetDescriptorSet());
                commandContext.Dispatch((meshletInstanceCount + MeshletCullThreadCount - 1u) / MeshletCullThreadCount, 1, 1);
                commandContext.InsertDescriptorSetOutputBarriers(cullShader.GetDescriptorSet());

                Shader& indirectShader = *demo.m_GBufferMeshletIndirectShader;
                commandContext.BindPipeline(indirectShader);
                if (indirectShader.HasShaderResourceView("BindlessTextures"))
                {
                    indirectShader.SetShaderResourceViews(cmd, "BindlessTextures", sceneTextures);
                }
                indirectShader.SetStructuredBuffer(cmd, "MeshletVertices", demo.m_SceneResources.GetMeshletVertexBuffer());
                indirectShader.SetShaderResource(cmd, "MeshletIndices", demo.m_SceneResources.GetMeshletIndexBuffer());
                indirectShader.SetStructuredBuffer(cmd, "Meshlets", demo.m_SceneResources.GetMeshletBuffer());
                indirectShader.SetStructuredBuffer(cmd, "MeshletTransforms", demo.m_SceneResources.GetMeshletTransformBuffer());
                indirectShader.SetStructuredBuffer(cmd, "MeshletInstances", demo.m_SceneResources.GetMeshletInstanceBuffer());
                indirectShader.SetStructuredBuffer(cmd, "MeshletMaterials", demo.m_SceneResources.GetMaterialBuffer());
                cmd.SetConstantBuffer(demo.m_GBufferMeshletIndirectShader, "PipelineCBuffer", demo.BuildPipelineConstants());
                commandContext.BindDescriptorSet(indirectShader.GetDescriptorSet());
                cmd.SetPrimitiveTopology(Mesh::PRIMITIVE_TOPOLOGY);
                commandContext.DrawIndirect(
                    *demo.m_MeshletDrawCommandSignature,
                    meshletInstanceCount,
                    demo.m_SceneResources.GetMeshletIndirectCommandBuffer());
                return;
            }
//Modify End
            commandContext.BindPipeline(*demo.m_GBufferShader);
//Modify Begin:2026-07-30 by BestHui
            commandContext.BindBindlessDescriptorHeap(demo.m_SceneResources.GetBindlessDescriptorHeap());
//Modify End
//Modify Begin:2026-07-30 by BestHui
            if (demo.m_GBufferShader->HasShaderResourceView("BindlessTextures"))
            {
                demo.m_GBufferShader->SetShaderResourceViews(cmd, "BindlessTextures", sceneTextures);
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
