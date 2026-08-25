#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Geometry/Model.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <Scene/SceneLightManager.h>

using namespace DirectX;

//Modify Begin:2026-08-19 by Hui
namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct BaseResourcesPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        bool UsesComputeMeshletIndirect = false;
    };

    struct MeshletPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };

    struct MeshletCullConstants
    {
        XMFLOAT4 FrustumPlanes[6] = {};
        uint32_t InstanceCount = 0;
        uint32_t DebugDisableCulling = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    bool UsesComputeMeshletIndirect(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const RaytracingDemoFrameState& frameState = *config.FrameState;
        return frameState.UseMeshletGBuffer &&
            !frameState.UseTaskShaderMeshlets &&
            resources.Scene.GetMeshletGpuResources().IsValid();
    }

    void DeclareGBufferOutputs(RenderGraph::RenderGraphPassBuilder& passBuilder)
    {
        passBuilder.WriteTexture(DemoResourceIds::GBufferAlbedoOcclusion);
        passBuilder.WriteTexture(DemoResourceIds::GBufferSpecularSmoothness);
        passBuilder.WriteTexture(DemoResourceIds::GBufferNormal);
        passBuilder.WriteTexture(DemoResourceIds::GBufferEmissionMetallic);
        passBuilder.WriteTexture(DemoResourceIds::GBufferPosition);
        passBuilder.WriteTexture(DemoResourceIds::MotionVector);
        passBuilder.WriteDepth(DemoResourceIds::DepthBuffer);
    }

    void DeclareGBufferShaderResources(
        RenderGraph::RenderGraphPassBuilder& passBuilder,
        const RaytracingDemoPassResources& resources)
    {
        resources.Scene.ForEachGBufferShaderResource(
            [&passBuilder](const Resource& resource)
            {
                passBuilder.ReadExternal(resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            });
    }

    void DeclareMeshletShaderResources(
        RenderGraph::RenderGraphPassBuilder& passBuilder,
        const MeshletGpuResources& meshletResources)
    {
        Assert(meshletResources.IsValid(), "Meshlet resources must be initialized before graph construction.");
        passBuilder.ReadExternal(*meshletResources.Vertices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        passBuilder.ReadExternal(*meshletResources.Indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        passBuilder.ReadExternal(*meshletResources.Meshlets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        passBuilder.ReadExternal(*meshletResources.Transforms, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        passBuilder.ReadExternal(*meshletResources.Instances, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    MeshletCullConstants BuildMeshletCullConstants(
        const RaytracingDemoPassResources& resources,
        const MeshletGpuResources& meshletResources)
    {
        MeshletCullConstants constants = {};
        const Camera::Frustum frustum = resources.SceneCamera.GetFrustum();
        for (uint32_t planeIndex = 0; planeIndex < Camera::Frustum::PLANES_COUNT; ++planeIndex)
        {
            const Camera::FrustumPlane& plane = frustum.m_Planes[planeIndex];
            constants.FrustumPlanes[planeIndex] = {
                plane.m_Normal.x,
                plane.m_Normal.y,
                plane.m_Normal.z,
                plane.m_Distance,
            };
        }
        constants.InstanceCount = meshletResources.InstanceCount;
        return constants;
    }

    template<typename ShaderType>
    void BindMeshletDrawResources(
        CommandContext& commandContext,
        ShaderType& shader,
        const RaytracingDemoPassResources& resources,
        const MeshletGpuResources& meshletResources)
    {
        if (shader.GetDescriptorSet().HasBinding("BindlessTextures", DescriptorBindingKind::ShaderResourceView))
        {
            commandContext.SetShaderResourceViews(
                shader,
                "BindlessTextures",
                resources.Scene.GetTextureShaderResourceViews());
        }
        commandContext.SetStructuredBuffer(shader, "MeshletVertices", *meshletResources.Vertices);
        commandContext.SetShaderResource(shader, "MeshletIndices", *meshletResources.Indices);
        commandContext.SetStructuredBuffer(shader, "Meshlets", *meshletResources.Meshlets);
        commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
        commandContext.SetStructuredBuffer(shader, "MeshletInstances", *meshletResources.Instances);
        commandContext.SetStructuredBuffer(shader, "MeshletMaterials", resources.Scene.GetMaterialBuffer());
    }

    void UpdateSceneGpuResources(
        const RaytracingDemoPassResources& resources,
        CommandList& commandList,
        const RenderGraph::RenderContext& context)
    {
        if (resources.Lights.Upload(commandList, context.GetMetadata().m_FrameIndex))
        {
            resources.Pipelines.BindRayTracingResources(
                resources.Scene.GetRayTracingAccelerationStructure(),
                resources.Scene,
                resources.Lights,
                resources.SkyboxTexture);
        }
    }

    void RecordRasterGBuffer(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        CommandList& commandList)
    {
        const RaytracingDemoFrameState& frameState = *config.FrameState;
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        commandContext.BindPipeline(*resources.GBufferShader);
        if (resources.GBufferShader->HasShaderResourceView("BindlessTextures"))
        {
            commandContext.SetShaderResourceViews(
                *resources.GBufferShader,
                "BindlessTextures",
                resources.Scene.GetTextureShaderResourceViews());
        }

        RaytracingDemoGBufferDebugConstants debugConstants = {};
        commandContext.SetConstantBuffer(*resources.GBufferShader, "GBufferDebugCBuffer", debugConstants);

        const RaytracingDemoPipelineConstants pipelineConstants = BuildPassPipelineConstants(resources, config);
        const XMMATRIX viewProjection = pipelineConstants.ViewProjection;
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

            RaytracingDemoModelConstants modelConstants = {};
            modelConstants.Model = object.WorldMatrix;
            modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
            modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
            modelConstants.PreviousModelViewProjection = object.WorldMatrix * previousViewProjection;
            commandContext.SetConstantBuffer(*resources.GBufferShader, "ModelCBuffer", modelConstants);

            RaytracingDemoGBufferMaterialConstants materialConstants = {};
            materialConstants.Diffuse = material.Diffuse;
            materialConstants.Specular = material.Specular;
            materialConstants.Emission = material.Emission;
            materialConstants.TilingOffset = material.TilingOffset;
            materialConstants.DiffuseTextureIndex = material.DiffuseTextureIndex;
            materialConstants.NormalTextureIndex = material.NormalTextureIndex;
            materialConstants.MetallicTextureIndex = material.MetallicTextureIndex;
            materialConstants.RoughnessTextureIndex = material.RoughnessTextureIndex;
            materialConstants.AmbientOcclusionTextureIndex = material.AmbientOcclusionTextureIndex;
            materialConstants.EmissionTextureIndex = material.EmissionTextureIndex;
            materialConstants.Metallic = material.Metallic;
            materialConstants.Roughness = material.Roughness;
            materialConstants.HasDiffuseMap = material.HasDiffuseMap;
            materialConstants.HasNormalMap = material.HasNormalMap;
            materialConstants.HasMetallicMap = material.HasMetallicMap;
            materialConstants.HasRoughnessMap = material.HasRoughnessMap;
            materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
            materialConstants.HasEmissionMap = material.HasEmissionMap;
            commandContext.SetConstantBuffer(*resources.GBufferShader, "MaterialCBuffer", materialConstants);

            commandContext.BindDescriptorSet(resources.GBufferShader->GetDescriptorSet());
            for (const auto& mesh : geometry.Model->GetMeshes())
            {
                mesh->Bind(commandList);
                commandContext.DrawIndexed(mesh->GetIndexCount());
            }
        }
    }

    void RecordTaskMeshGBuffer(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources)
    {
        constexpr uint32_t meshletTaskGroupSize = 32u;
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        MeshShader& shader = *resources.GBufferTaskMeshShader;
        commandContext.BindPipeline(shader);
        BindMeshletDrawResources(commandContext, shader, resources, meshletResources);
        commandContext.SetConstantBuffer(shader, "MeshletCullCBuffer", BuildMeshletCullConstants(resources, meshletResources));
        commandContext.SetConstantBuffer(shader, "PipelineCBuffer", BuildPassPipelineConstants(resources, config));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.DispatchMesh(
            (meshletResources.InstanceCount + meshletTaskGroupSize - 1u) / meshletTaskGroupSize,
            1u,
            1u);
    }

    void RecordMeshletCull(
        const RaytracingDemoPassResources& resources,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources)
    {
        constexpr uint32_t meshletCullThreadCount = 64u;
        CommandContext commandContext(commandList);
        ComputeShader& shader = *resources.MeshletCullShader;
        commandContext.BindPipeline(shader);
        commandContext.SetStructuredBuffer(shader, "Meshlets", *meshletResources.Meshlets);
        commandContext.SetStructuredBuffer(shader, "MeshletInstances", *meshletResources.Instances);
        commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletIndirectCommands",
            UnorderedAccessView(*meshletResources.IndirectCommands));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletIndirectCount",
            UnorderedAccessView(meshletResources.IndirectCommands->GetCounterBuffer()));
        commandContext.SetConstantBuffer(shader, "MeshletCullCBuffer", BuildMeshletCullConstants(resources, meshletResources));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.Dispatch(
            (meshletResources.InstanceCount + meshletCullThreadCount - 1u) / meshletCullThreadCount,
            1u,
            1u);
    }

    void RecordMeshletIndirectGBuffer(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources)
    {
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        Shader& shader = *resources.GBufferMeshletIndirectShader;
        commandContext.BindPipeline(shader);
        BindMeshletDrawResources(commandContext, shader, resources, meshletResources);
        commandContext.SetConstantBuffer(shader, "PipelineCBuffer", BuildPassPipelineConstants(resources, config));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandList.SetPrimitiveTopology(Mesh::PRIMITIVE_TOPOLOGY);
        commandContext.DrawIndirect(
            *resources.MeshletDrawCommandSignature,
            IndirectCommandExecutionDesc{
                .ArgumentBuffer = meshletResources.IndirectCommands,
                .MaxCommandCount = meshletResources.InstanceCount,
                .CountBuffer = &meshletResources.IndirectCommands->GetCounterBuffer(),
            });
    }

    void AddMeshletCounterResetPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Indirect Counter Reset",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::SceneResourcesReadyToken);
                passBuilder.WriteExternal(
                    meshletResources.IndirectCommands->GetCounterBuffer(),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteToken(DemoResourceIds::MeshletCounterResetToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const MeshletGpuResources meshletResources = passData.Resources->Scene.GetMeshletGpuResources();
                const UINT clearValues[4] = {};
                CommandContext(commandList).ClearUnorderedAccessUint(
                    meshletResources.IndirectCommands->GetCounterBuffer(),
                    clearValues);
            });
    }

    void AddMeshletCullPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Cull",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletCounterResetToken);
                DeclareMeshletShaderResources(passBuilder, meshletResources);
                passBuilder.WriteExternal(
                    *meshletResources.IndirectCommands,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    true);
                passBuilder.WriteToken(DemoResourceIds::MeshletCullFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordMeshletCull(resources, commandList, resources.Scene.GetMeshletGpuResources());
            });
    }

    void AddMeshletIndirectGBufferPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Indirect GBuffer",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletCullFinishedToken);
                DeclareGBufferOutputs(passBuilder);
                DeclareGBufferShaderResources(passBuilder, resources);
                DeclareMeshletShaderResources(passBuilder, meshletResources);
                passBuilder.ReadIndirectArgument(*meshletResources.IndirectCommands);
                passBuilder.WriteToken(DemoResourceIds::BaseResourcesFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordMeshletIndirectGBuffer(
                    resources,
                    passData.Config,
                    commandList,
                    resources.Scene.GetMeshletGpuResources());
            });
    }
}

void RaytracingDemoPasses::Builder::AddBaseResourcesPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    const bool useComputeMeshletIndirect = UsesComputeMeshletIndirect(resources, config);
    const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
    renderGraphBuilder.AddPass<BaseResourcesPassData>(
        useComputeMeshletIndirect ? L"Scene Resource Updates" : L"Base Resources",
        [&resources, config, useComputeMeshletIndirect, meshletResources](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            BaseResourcesPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.UsesComputeMeshletIndirect = useComputeMeshletIndirect;
            if (config.FrameState->DynamicRayTracingUpdateEnabled)
            {
                passBuilder.ReadToken(DemoResourceIds::DynamicRayTracingUpdatedToken);
            }
            if (useComputeMeshletIndirect)
            {
                passBuilder.WriteToken(DemoResourceIds::SceneResourcesReadyToken);
                return;
            }

            DeclareGBufferOutputs(passBuilder);
            DeclareGBufferShaderResources(passBuilder, resources);
            const bool useTaskMeshShaders = config.FrameState->UseMeshletGBuffer &&
                config.FrameState->UseTaskShaderMeshlets &&
                resources.GBufferTaskMeshShader != nullptr &&
                meshletResources.IsValid();
            if (useTaskMeshShaders)
            {
                DeclareMeshletShaderResources(passBuilder, meshletResources);
            }
            passBuilder.WriteToken(DemoResourceIds::BaseResourcesFinishedToken);
        },
        [](const BaseResourcesPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            UpdateSceneGpuResources(resources, commandList, context);
            if (passData.UsesComputeMeshletIndirect)
            {
                return;
            }

            const RaytracingDemoFrameState& frameState = *passData.Config.FrameState;
            const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
            if (frameState.UseMeshletGBuffer &&
                frameState.UseTaskShaderMeshlets &&
                resources.GBufferTaskMeshShader != nullptr &&
                meshletResources.IsValid())
            {
                RecordTaskMeshGBuffer(resources, passData.Config, commandList, meshletResources);
                return;
            }
            RecordRasterGBuffer(resources, passData.Config, commandList);
        });

    if (!useComputeMeshletIndirect)
    {
        return;
    }

    AddMeshletCounterResetPass(renderGraphBuilder, resources, config);
    AddMeshletCullPass(renderGraphBuilder, resources, config);
    AddMeshletIndirectGBufferPass(renderGraphBuilder, resources, config);
}
//Modify End
