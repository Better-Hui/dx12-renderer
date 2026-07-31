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
            commandContext.BindBindlessDescriptorHeap(demo.m_SceneResources.GetBindlessDescriptorHeap());
            MeshletGpuResources meshletResources = demo.m_SceneResources.GetMeshletGpuResources();
            const bool useMeshletGBuffer = demo.m_UseMeshletGBuffer && meshletResources.IsValid();
            RaytracingDemo::GBufferDebugConstants debugConstants{};
            debugConstants.DebugMeshletClusters = (useMeshletGBuffer && demo.m_DebugMeshletClusters) ? 1u : 0u;
//Modify Begin:2026-07-30 by BestHui
            const std::vector<ShaderResourceView> sceneTextures = demo.m_SceneResources.CreateTextureShaderResourceViews();
//Modify End
            if (useMeshletGBuffer)
            {
                constexpr uint32_t MeshletCullThreadCount = 64;
                const uint32_t meshletInstanceCount = meshletResources.InstanceCount;
                uint32_t clearCounter = 0;
                StructuredBuffer& meshletIndirectCommands = *meshletResources.IndirectCommands;

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
//Modify Begin:2026-07-31 by BestHui
                cullConstants.DebugDisableCulling = 0u;
//Modify End

//Modify Begin:2026-07-31 by BestHui
                const auto bindMeshletDrawResources = [&](auto& shader)
                {
                    if (shader.GetDescriptorSet().HasBinding("BindlessTextures", DescriptorBindingKind::ShaderResourceView))
                    {
                        commandContext.SetShaderResourceViews(shader, "BindlessTextures", sceneTextures);
                    }
                    commandContext.SetStructuredBuffer(shader, "MeshletVertices", *meshletResources.Vertices);
                    commandContext.SetShaderResource(shader, "MeshletIndices", *meshletResources.Indices);
                    commandContext.SetStructuredBuffer(shader, "Meshlets", *meshletResources.Meshlets);
                    commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
                    commandContext.SetStructuredBuffer(shader, "MeshletInstances", *meshletResources.Instances);
                    commandContext.SetStructuredBuffer(shader, "MeshletMaterials", demo.m_SceneResources.GetMaterialBuffer());
                };

                if (demo.m_UseTaskShaderMeshlets && demo.m_GBufferTaskMeshShader != nullptr)
                {
                    constexpr uint32_t MeshletTaskGroupSize = 32;
                    MeshShader& taskMeshShader = *demo.m_GBufferTaskMeshShader;
                    commandContext.BindPipeline(taskMeshShader);
                    bindMeshletDrawResources(taskMeshShader);
                    commandContext.SetConstantBuffer(taskMeshShader, "MeshletCullCBuffer", sizeof(cullConstants), &cullConstants);
                    const RaytracingDemo::PipelineConstants pipelineConstants = demo.BuildPipelineConstants();
                    commandContext.SetConstantBuffer(taskMeshShader, "PipelineCBuffer", sizeof(pipelineConstants), &pipelineConstants);
                    commandContext.BindDescriptorSet(taskMeshShader.GetDescriptorSet());
                    commandContext.DispatchMesh((meshletInstanceCount + MeshletTaskGroupSize - 1u) / MeshletTaskGroupSize, 1, 1);
                    return;
                }
//Modify End

                cmd.CopyByteAddressBuffer(meshletIndirectCommands.GetCounterBuffer(), clearCounter);
                cmd.TransitionBarrier(meshletIndirectCommands.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                ComputeShader& cullShader = *demo.m_MeshletCullShader;
                commandContext.BindPipeline(cullShader);
//Modify Begin:2026-07-31 by BestHui
                commandContext.SetStructuredBuffer(cullShader, "Meshlets", *meshletResources.Meshlets);
                commandContext.SetStructuredBuffer(cullShader, "MeshletInstances", *meshletResources.Instances);
                commandContext.SetStructuredBuffer(cullShader, "MeshletTransforms", *meshletResources.Transforms);
                commandContext.SetUnorderedAccessView(
                    cullShader,
                    "MeshletIndirectCommands",
                    UnorderedAccessView(meshletIndirectCommands));
                commandContext.SetUnorderedAccessView(
                    cullShader,
                    "MeshletIndirectCount",
                    UnorderedAccessView(meshletIndirectCommands.GetCounterBuffer()));
                commandContext.SetConstantBuffer(cullShader, "MeshletCullCBuffer", sizeof(cullConstants), &cullConstants);
//Modify End
                commandContext.BindDescriptorSet(cullShader.GetDescriptorSet());
                commandContext.Dispatch((meshletInstanceCount + MeshletCullThreadCount - 1u) / MeshletCullThreadCount, 1, 1);
                commandContext.InsertDescriptorSetOutputBarriers(cullShader.GetDescriptorSet());

                Shader& indirectShader = *demo.m_GBufferMeshletIndirectShader;
                commandContext.BindPipeline(indirectShader);
//Modify Begin:2026-07-31 by BestHui
                bindMeshletDrawResources(indirectShader);
//Modify End
                const RaytracingDemo::PipelineConstants pipelineConstants = demo.BuildPipelineConstants();
                commandContext.SetConstantBuffer(indirectShader, "PipelineCBuffer", sizeof(pipelineConstants), &pipelineConstants);
                commandContext.BindDescriptorSet(indirectShader.GetDescriptorSet());
                cmd.SetPrimitiveTopology(Mesh::PRIMITIVE_TOPOLOGY);
                commandContext.DrawIndirect(
                    *demo.m_MeshletDrawCommandSignature,
                    meshletInstanceCount,
                    meshletIndirectCommands);
                return;
            }
//Modify End
            commandContext.BindPipeline(*demo.m_GBufferShader);
//Modify Begin:2026-07-30 by BestHui
            if (demo.m_GBufferShader->HasShaderResourceView("BindlessTextures"))
            {
//Modify Begin:2026-07-31 by BestHui
                commandContext.SetShaderResourceViews(*demo.m_GBufferShader, "BindlessTextures", sceneTextures);
//Modify End
            }
//Modify End
            commandContext.SetConstantBuffer(*demo.m_GBufferShader, "GBufferDebugCBuffer", sizeof(debugConstants), &debugConstants);
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
                commandContext.SetConstantBuffer(*demo.m_GBufferShader, "ModelCBuffer", sizeof(modelConstants), &modelConstants);

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
                commandContext.SetConstantBuffer(*demo.m_GBufferShader, "MaterialCBuffer", sizeof(materialConstants), &materialConstants);

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
