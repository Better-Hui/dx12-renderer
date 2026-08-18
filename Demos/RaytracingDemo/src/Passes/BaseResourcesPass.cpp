#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-18 by Hui
#include <DX12Library/ResourceUploader.h>
//Modify End
#include <Framework/Geometry/Mesh.h>
//Modify Begin:2026-07-30 by Hui
#include <Framework/Geometry/Model.h>
//Modify End
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
//Modify End
#include <RenderGraph/RenderGraphBuilder.h>
//Modify Begin:2026-07-30 by Hui
#include <Scene/SceneLightManager.h>
//Modify End

using namespace DirectX;

//Modify Begin:2026-08-18 by Hui
namespace
{
    struct BaseResourcesPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };
}
//Modify End

//Modify Begin:2026-07-30 by Hui
void RaytracingDemoPasses::Builder::AddBaseResourcesPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<BaseResourcesPassData>(
        L"Base Resources",
        [&resources, config](RenderGraphPassBuilder& passBuilder, BaseResourcesPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passBuilder.WriteTexture(DemoResourceIds::GBufferAlbedoOcclusion);
            passBuilder.WriteTexture(DemoResourceIds::GBufferSpecularSmoothness);
            passBuilder.WriteTexture(DemoResourceIds::GBufferNormal);
            passBuilder.WriteTexture(DemoResourceIds::GBufferEmissionMetallic);
            passBuilder.WriteTexture(DemoResourceIds::GBufferPosition);
            passBuilder.WriteTexture(DemoResourceIds::MotionVector);
            passBuilder.WriteDepth(DemoResourceIds::DepthBuffer);
            passBuilder.WriteToken(DemoResourceIds::BaseResourcesFinishedToken);
            resources.Scene.ForEachGBufferShaderResource(
                [&passBuilder](const Resource& resource)
                {
                    passBuilder.ReadExternal(resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                });
        },
        [](const BaseResourcesPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
//Modify Begin:2026-07-30 by Hui
            const RaytracingDemoFrameState& frameState = *config.FrameState;
//Modify End
//Modify Begin:2026-07-29 by Hui
//Modify Begin:2026-07-30 by Hui
            if (resources.Lights.Upload(cmd, context.GetMetadata().m_FrameIndex))
            {
                resources.Pipelines.BindRayTracingResources(
                    resources.Scene.GetRayTracingAccelerationStructure(),
                    resources.Scene,
                    resources.Lights,
                    resources.SkyboxTexture);
            }
//Modify End
            CommandContext commandContext(cmd);
//Modify Begin:2026-07-30 by Hui
            commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
            const bool useMeshletGBuffer = frameState.UseMeshletGBuffer && meshletResources.IsValid();
            RaytracingDemoGBufferDebugConstants debugConstants{};
            debugConstants.DebugMeshletClusters = useMeshletGBuffer &&
                frameState.DebugMeshletClusters ? 1u : 0u;
//Modify Begin:2026-07-30 by Hui
//Modify Begin:2026-08-11 by Hui
            const std::vector<ShaderResourceView>& sceneTextures = resources.Scene.GetTextureShaderResourceViews();
//Modify End
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

                const Camera::Frustum frustum = resources.SceneCamera.GetFrustum();
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
//Modify Begin:2026-07-31 by Hui
                cullConstants.DebugDisableCulling = 0u;
//Modify End

//Modify Begin:2026-07-31 by Hui
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
                    commandContext.SetStructuredBuffer(shader, "MeshletMaterials", resources.Scene.GetMaterialBuffer());
                };

                if (frameState.UseTaskShaderMeshlets && resources.GBufferTaskMeshShader != nullptr)
                {
                    constexpr uint32_t MeshletTaskGroupSize = 32;
                    MeshShader& taskMeshShader = *resources.GBufferTaskMeshShader;
                    commandContext.BindPipeline(taskMeshShader);
                    bindMeshletDrawResources(taskMeshShader);
                    commandContext.SetConstantBuffer(taskMeshShader, "MeshletCullCBuffer", sizeof(cullConstants), &cullConstants);
                    const RaytracingDemoPipelineConstants pipelineConstants = BuildPassPipelineConstants(resources, config);
                    commandContext.SetConstantBuffer(taskMeshShader, "PipelineCBuffer", sizeof(pipelineConstants), &pipelineConstants);
                    commandContext.BindDescriptorSet(taskMeshShader.GetDescriptorSet());
                    commandContext.DispatchMesh((meshletInstanceCount + MeshletTaskGroupSize - 1u) / MeshletTaskGroupSize, 1, 1);
                    return;
                }
//Modify End

//Modify Begin:2026-08-18 by Hui
                ResourceUploader(cmd.GetDeviceContext()).UploadByteAddressBuffer(
                    cmd, meshletIndirectCommands.GetCounterBuffer(), clearCounter);
//Modify End
                commandContext.TransitionUnorderedAccess(meshletIndirectCommands.GetCounterBuffer());

                ComputeShader& cullShader = *resources.MeshletCullShader;
                commandContext.BindPipeline(cullShader);
//Modify Begin:2026-07-31 by Hui
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

                Shader& indirectShader = *resources.GBufferMeshletIndirectShader;
                commandContext.BindPipeline(indirectShader);
//Modify Begin:2026-07-31 by Hui
                bindMeshletDrawResources(indirectShader);
//Modify End
                const RaytracingDemoPipelineConstants pipelineConstants = BuildPassPipelineConstants(resources, config);
                commandContext.SetConstantBuffer(indirectShader, "PipelineCBuffer", sizeof(pipelineConstants), &pipelineConstants);
                commandContext.BindDescriptorSet(indirectShader.GetDescriptorSet());
                cmd.SetPrimitiveTopology(Mesh::PRIMITIVE_TOPOLOGY);
                commandContext.DrawIndirect(
                    *resources.MeshletDrawCommandSignature,
                    meshletInstanceCount,
                    meshletIndirectCommands);
                return;
            }
//Modify End
            commandContext.BindPipeline(*resources.GBufferShader);
//Modify Begin:2026-07-30 by Hui
            if (resources.GBufferShader->HasShaderResourceView("BindlessTextures"))
            {
//Modify Begin:2026-07-31 by Hui
                commandContext.SetShaderResourceViews(*resources.GBufferShader, "BindlessTextures", sceneTextures);
//Modify End
            }
//Modify End
            commandContext.SetConstantBuffer(*resources.GBufferShader, "GBufferDebugCBuffer", sizeof(debugConstants), &debugConstants);
//Modify End

//Modify Begin:2026-08-07 by Hui
            const RaytracingDemoPipelineConstants pipelineConstants = BuildPassPipelineConstants(resources, config);
            const XMMATRIX viewProjection = pipelineConstants.ViewProjection;
//Modify End
            const XMMATRIX previousViewProjection = frameState.HasPreviousViewProjection
                ? frameState.PreviousViewProjection
                : viewProjection;
            const auto& sceneObjects = resources.Scene.GetSceneObjects();
            const auto& sceneGeometries = resources.Scene.GetSceneGeometries();
            const auto& materials = resources.Scene.GetMaterials();
            for (const RaytracingDemoSceneObject& object : sceneObjects)
            {
                const RaytracingDemoMaterialData& material = materials[object.MaterialIndex];
                const RaytracingDemoSceneGeometry& geometry = sceneGeometries[object.GeometryIndex];

                RaytracingDemoModelConstants modelConstants{};
                modelConstants.Model = object.WorldMatrix;
                modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
                modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
                modelConstants.PreviousModelViewProjection = object.WorldMatrix * previousViewProjection;
                commandContext.SetConstantBuffer(*resources.GBufferShader, "ModelCBuffer", sizeof(modelConstants), &modelConstants);

                RaytracingDemoGBufferMaterialConstants materialConstants{};
                materialConstants.Diffuse = material.Diffuse;
                materialConstants.Specular = material.Specular;
                materialConstants.Emission = material.Emission;
                materialConstants.TilingOffset = material.TilingOffset;
//Modify Begin:2026-07-30 by Hui
                materialConstants.DiffuseTextureIndex = material.DiffuseTextureIndex;
                materialConstants.NormalTextureIndex = material.NormalTextureIndex;
                materialConstants.MetallicTextureIndex = material.MetallicTextureIndex;
                materialConstants.RoughnessTextureIndex = material.RoughnessTextureIndex;
                materialConstants.AmbientOcclusionTextureIndex = material.AmbientOcclusionTextureIndex;
                materialConstants.EmissionTextureIndex = material.EmissionTextureIndex;
//Modify End
                materialConstants.Metallic = material.Metallic;
                materialConstants.Roughness = material.Roughness;
                materialConstants.HasDiffuseMap = material.HasDiffuseMap;
                materialConstants.HasNormalMap = material.HasNormalMap;
                materialConstants.HasMetallicMap = material.HasMetallicMap;
                materialConstants.HasRoughnessMap = material.HasRoughnessMap;
                materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
                materialConstants.HasEmissionMap = material.HasEmissionMap;
                commandContext.SetConstantBuffer(*resources.GBufferShader, "MaterialCBuffer", sizeof(materialConstants), &materialConstants);

//Modify Begin:2026-07-29 by Hui
                commandContext.BindDescriptorSet(resources.GBufferShader->GetDescriptorSet());
//Modify End
//Modify Begin:2026-07-30 by Hui
                for (const auto& mesh : geometry.Model->GetMeshes())
                {
                    mesh->Bind(cmd);
                    commandContext.DrawIndexed(mesh->GetIndexCount());
                }
//Modify End
            }
        });
}
//Modify End
