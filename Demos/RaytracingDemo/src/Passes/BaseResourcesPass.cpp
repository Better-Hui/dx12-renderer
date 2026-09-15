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

//Modify Begin:2026-08-25 by Hui
namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    constexpr uint32_t MeshletCullThreadCount = 64u;
    constexpr uint32_t MeshletTaskGroupSize = 32u;

    struct BaseResourcesPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        bool UsesMeshletPipeline = false;
    };

    struct MeshletPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };

    struct MeshletCullConstants
    {
        XMFLOAT4 FrustumPlanes[6] = {};
        uint32_t DrawCount = 0;
        uint32_t CandidateCapacity = 0;
        uint32_t BackendGroupSize = 0;
        uint32_t DebugDisableCulling = 0;
        uint32_t EnableInstanceCull = 1;
        uint32_t WriteDrawCommands = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    bool UsesComputeMeshletIndirect(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const RaytracingDemoFrameState& frameState = *config.FrameState;
        return frameState.UseMeshletGBuffer &&
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
        passBuilder.ReadExternal(*meshletResources.Draws, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void DeclareMeshletIndirectDrawResources(
        RenderGraph::RenderGraphPassBuilder& passBuilder,
        const MeshletGpuResources& meshletResources)
    {
        Assert(meshletResources.IsValid(), "Meshlet resources must be initialized before graph construction.");
        passBuilder.ReadExternal(*meshletResources.Vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        passBuilder.ReadExternal(*meshletResources.Indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        passBuilder.ReadExternal(*meshletResources.Transforms, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        passBuilder.ReadExternal(*meshletResources.VisibleInstances, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    MeshletCullConstants BuildMeshletCullConstants(
        const RaytracingDemoPassResources& resources,
        const MeshletGpuResources& meshletResources,
        const uint32_t backendGroupSize,
        const bool writeDrawCommands,
        const bool enableInstanceCull = true)
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
        constants.DrawCount = meshletResources.DrawCount;
        constants.CandidateCapacity = meshletResources.CandidateCapacity;
        constants.BackendGroupSize = backendGroupSize;
        constants.WriteDrawCommands = writeDrawCommands ? 1u : 0u;
        constants.EnableInstanceCull = enableInstanceCull ? 1u : 0u;
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
        commandContext.SetStructuredBuffer(shader, "MeshletInstances", *meshletResources.VisibleInstances);
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
        commandContext.SetConstantBuffer(*resources.GBufferShader, "PipelineCBuffer", pipelineConstants);
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
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        MeshShader& shader = *resources.GBufferTaskMeshShader;
        commandContext.BindPipeline(shader);
        BindMeshletDrawResources(commandContext, shader, resources, meshletResources);
        commandContext.SetShaderResource(shader, "MeshletVisibleCount", meshletResources.VisibleInstances->GetCounterBuffer());
        commandContext.SetConstantBuffer(shader, "PipelineCBuffer", BuildPassPipelineConstants(resources, config));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.DispatchMeshIndirect(
            *resources.MeshletDispatchMeshCommandSignature,
            IndirectCommandExecutionDesc{
                .ArgumentBuffer = meshletResources.MeshDispatchArguments,
                .MaxCommandCount = 1u,
            });
    }

    void RecordMeshletInstanceCull(
        const RaytracingDemoPassResources& resources,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources,
        const bool enableInstanceCull)
    {
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        ComputeShader& shader = *resources.MeshletInstanceCullShader;
        commandContext.BindPipeline(shader);
        commandContext.SetStructuredBuffer(shader, "MeshletDraws", *meshletResources.Draws);
        commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletVisibleDrawIndices",
            UnorderedAccessView(*meshletResources.VisibleDrawIndices));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletCandidateExpandDispatchArguments",
            UnorderedAccessView(*meshletResources.CandidateExpandDispatchArguments));
        commandContext.SetConstantBuffer(
            shader,
            "MeshletCullCBuffer",
            BuildMeshletCullConstants(resources, meshletResources, MeshletCullThreadCount, false,
                enableInstanceCull));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.Dispatch(
            (meshletResources.DrawCount + MeshletCullThreadCount - 1u) / MeshletCullThreadCount,
            1u,
            1u);
    }

    void RecordMeshletCandidateExpand(
        const RaytracingDemoPassResources& resources,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources)
    {
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        ComputeShader& shader = *resources.MeshletCandidateExpandShader;
        commandContext.BindPipeline(shader);
        commandContext.SetStructuredBuffer(shader, "MeshletDraws", *meshletResources.Draws);
        commandContext.SetStructuredBuffer(shader, "MeshletVisibleDrawIndices", *meshletResources.VisibleDrawIndices);
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletCandidateInstances",
            UnorderedAccessView(*meshletResources.CandidateInstances));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletFineCullDispatchArguments",
            UnorderedAccessView(*meshletResources.FineCullDispatchArguments));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletCandidateCount",
            UnorderedAccessView(meshletResources.CandidateInstances->GetCounterBuffer()));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.DispatchIndirect(
            *resources.MeshletComputeDispatchCommandSignature,
            IndirectCommandExecutionDesc{
                .ArgumentBuffer = meshletResources.CandidateExpandDispatchArguments,
                .MaxCommandCount = 1u,
            });
    }

    void RecordMeshletFineCull(
        const RaytracingDemoPassResources& resources,
        CommandList& commandList,
        const MeshletGpuResources& meshletResources,
        const bool writeDrawCommands)
    {
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        ComputeShader& shader = *resources.MeshletCullShader;
        commandContext.BindPipeline(shader);
        commandContext.SetStructuredBuffer(shader, "Meshlets", *meshletResources.Meshlets);
        commandContext.SetStructuredBuffer(shader, "MeshletCandidateInstances", *meshletResources.CandidateInstances);
        commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
        commandContext.SetShaderResource(shader, "MeshletCandidateCount", 0u, meshletResources.CandidateInstances->GetCounterBuffer());
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletVisibleInstances",
            UnorderedAccessView(*meshletResources.VisibleInstances));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletVisibleCount",
            UnorderedAccessView(meshletResources.VisibleInstances->GetCounterBuffer()));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletIndirectCommands",
            UnorderedAccessView(*meshletResources.IndirectCommands));
        commandContext.SetUnorderedAccessView(
            shader,
            "MeshletMeshDispatchArguments",
            UnorderedAccessView(*meshletResources.MeshDispatchArguments));
        commandContext.SetConstantBuffer(
            shader,
            "MeshletCullCBuffer",
            BuildMeshletCullConstants(
                resources,
                meshletResources,
                writeDrawCommands ? 1u : MeshletTaskGroupSize,
                writeDrawCommands));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.DispatchIndirect(
            *resources.MeshletComputeDispatchCommandSignature,
            IndirectCommandExecutionDesc{
                .ArgumentBuffer = meshletResources.FineCullDispatchArguments,
                .MaxCommandCount = 1u,
            });
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
        if (shader.GetDescriptorSet().HasBinding("BindlessTextures", DescriptorBindingKind::ShaderResourceView))
        {
            commandContext.SetShaderResourceViews(
                shader,
                "BindlessTextures",
                resources.Scene.GetTextureShaderResourceViews());
        }
        commandContext.SetStructuredBuffer(shader, "MeshletTransforms", *meshletResources.Transforms);
        commandContext.SetStructuredBuffer(shader, "MeshletInstances", *meshletResources.VisibleInstances);
        commandContext.SetStructuredBuffer(shader, "MeshletMaterials", resources.Scene.GetMaterialBuffer());
        commandContext.SetConstantBuffer(shader, "PipelineCBuffer", BuildPassPipelineConstants(resources, config));
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandList.SetPrimitiveTopology(Mesh::PRIMITIVE_TOPOLOGY);
        commandList.SetVertexBufferView(0u, meshletResources.Vertices->GetVertexBufferView(), *meshletResources.Vertices);
        commandList.SetIndexBufferView(meshletResources.Indices->GetIndexBufferView(), *meshletResources.Indices);
        commandContext.DrawIndirect(
            *resources.MeshletDrawCommandSignature,
            IndirectCommandExecutionDesc{
                .ArgumentBuffer = meshletResources.IndirectCommands,
                .MaxCommandCount = meshletResources.CandidateCapacity,
                .CountBuffer = &meshletResources.VisibleInstances->GetCounterBuffer(),
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
                passBuilder.WriteExternal(*meshletResources.VisibleDrawIndices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(meshletResources.VisibleDrawIndices->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.CandidateExpandDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.FineCullDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.CandidateInstances, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(meshletResources.CandidateInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.VisibleInstances, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(meshletResources.VisibleInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.IndirectCommands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(meshletResources.IndirectCommands->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteExternal(*meshletResources.MeshDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                passBuilder.WriteToken(DemoResourceIds::MeshletCounterResetToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const MeshletGpuResources meshletResources = passData.Resources->Scene.GetMeshletGpuResources();
                const UINT clearValues[4] = {};
                CommandContext commandContext(commandList);
                commandContext.ClearUnorderedAccessUint(meshletResources.VisibleDrawIndices->GetCounterBuffer(), clearValues);
                commandContext.ClearUnorderedAccessUint(*meshletResources.CandidateExpandDispatchArguments, clearValues);
                commandContext.ClearUnorderedAccessUint(*meshletResources.FineCullDispatchArguments, clearValues);
                commandContext.ClearUnorderedAccessUint(meshletResources.CandidateInstances->GetCounterBuffer(), clearValues);
                commandContext.ClearUnorderedAccessUint(meshletResources.VisibleInstances->GetCounterBuffer(), clearValues);
                commandContext.ClearUnorderedAccessUint(meshletResources.IndirectCommands->GetCounterBuffer(), clearValues);
                commandContext.ClearUnorderedAccessUint(*meshletResources.MeshDispatchArguments, clearValues);
            });
    }

    void AddMeshletInstanceCullPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Instance Coarse Cull",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletCounterResetToken);
                passBuilder.ReadExternal(*meshletResources.Draws, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.Transforms, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.WriteExternal(*meshletResources.VisibleDrawIndices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(meshletResources.VisibleDrawIndices->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(*meshletResources.CandidateExpandDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteToken(DemoResourceIds::MeshletCullFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordMeshletInstanceCull(resources, commandList, resources.Scene.GetMeshletGpuResources(),
                    passData.Config.FrameState->UseMeshletInstanceCull);
            });
    }

    void AddMeshletCandidateExpandPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Candidate Expand",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletCullFinishedToken);
                passBuilder.ReadExternal(*meshletResources.Draws, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.VisibleDrawIndices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadIndirectArgument(*meshletResources.CandidateExpandDispatchArguments);
                passBuilder.WriteExternal(*meshletResources.FineCullDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(*meshletResources.CandidateInstances, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(meshletResources.CandidateInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteToken(DemoResourceIds::MeshletCandidateExpandFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordMeshletCandidateExpand(resources, commandList, resources.Scene.GetMeshletGpuResources());
            });
    }

    void AddMeshletFineCullPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        const bool writeDrawCommands = !config.FrameState->UseTaskShaderMeshlets;
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Fine Cull",
            [&resources, config, meshletResources, writeDrawCommands](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletCandidateExpandFinishedToken);
                passBuilder.ReadExternal(*meshletResources.Meshlets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.CandidateInstances, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.Transforms, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(meshletResources.CandidateInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadIndirectArgument(*meshletResources.FineCullDispatchArguments);
                passBuilder.WriteExternal(*meshletResources.VisibleInstances, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(meshletResources.VisibleInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(*meshletResources.IndirectCommands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(meshletResources.IndirectCommands->GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteExternal(*meshletResources.MeshDispatchArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
                passBuilder.WriteToken(DemoResourceIds::MeshletFineCullFinishedToken);
            },
            [writeDrawCommands](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordMeshletFineCull(resources, commandList, resources.Scene.GetMeshletGpuResources(), writeDrawCommands);
            });
    }

    void AddMeshletStatisticsReadbackPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Visibility Counter Readback",
            [&resources, config, meshletResources](
                RenderGraph::RenderGraphPassBuilder& passBuilder,
                MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(DemoResourceIds::MeshletFineCullFinishedToken);
                passBuilder.ReadExternal(
                    *meshletResources.CandidateExpandDispatchArguments,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                passBuilder.ReadExternal(
                    meshletResources.VisibleInstances->GetCounterBuffer(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                passBuilder.WriteToken(DemoResourceIds::MeshletStatisticsReadbackFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
                resources.MeshletCullingStatistics.RecordReadback(
                    commandList,
                    *meshletResources.CandidateExpandDispatchArguments,
                    meshletResources.VisibleInstances->GetCounterBuffer());
            });
    }

    void AddMeshletTaskGBufferPass(
        RenderGraph::RenderGraphBuilder& renderGraphBuilder,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config)
    {
        const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
        renderGraphBuilder.AddPass<MeshletPassData>(
            L"Meshlet Task Mesh GBuffer",
            [&resources, config, meshletResources](RenderGraph::RenderGraphPassBuilder& passBuilder, MeshletPassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.Config = config;
                passBuilder.ReadToken(config.FrameState->DebugMeshletClusters
                    ? DemoResourceIds::MeshletStatisticsReadbackFinishedToken
                    : DemoResourceIds::MeshletFineCullFinishedToken);
                DeclareGBufferOutputs(passBuilder);
                DeclareGBufferShaderResources(passBuilder, resources);
                passBuilder.ReadExternal(*meshletResources.Vertices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.Indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.Meshlets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.Transforms, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(*meshletResources.VisibleInstances, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadExternal(meshletResources.VisibleInstances->GetCounterBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                passBuilder.ReadIndirectArgument(*meshletResources.MeshDispatchArguments);
                passBuilder.WriteToken(DemoResourceIds::BaseResourcesFinishedToken);
            },
            [](const MeshletPassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                RecordTaskMeshGBuffer(resources, passData.Config, commandList, resources.Scene.GetMeshletGpuResources());
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
                passBuilder.ReadToken(config.FrameState->DebugMeshletClusters
                    ? DemoResourceIds::MeshletStatisticsReadbackFinishedToken
                    : DemoResourceIds::MeshletFineCullFinishedToken);
                DeclareGBufferOutputs(passBuilder);
                DeclareGBufferShaderResources(passBuilder, resources);
                DeclareMeshletIndirectDrawResources(passBuilder, meshletResources);
                passBuilder.ReadExternal(
                    meshletResources.VisibleInstances->GetCounterBuffer(),
                    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
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
    const bool useMeshletPipeline = UsesComputeMeshletIndirect(resources, config);
    const MeshletGpuResources meshletResources = resources.Scene.GetMeshletGpuResources();
    renderGraphBuilder.AddPass<BaseResourcesPassData>(
        useMeshletPipeline ? L"Scene Resource Updates" : L"Base Resources",
        [&resources, config, useMeshletPipeline, meshletResources](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            BaseResourcesPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.UsesMeshletPipeline = useMeshletPipeline;
            if (config.FrameState->DynamicRayTracingUpdateEnabled)
            {
                passBuilder.ReadToken(DemoResourceIds::DynamicRayTracingUpdatedToken);
            }
            if (useMeshletPipeline)
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
            if (passData.UsesMeshletPipeline)
            {
                return;
            }
            RecordRasterGBuffer(resources, passData.Config, commandList);
        });

    if (!useMeshletPipeline)
    {
        return;
    }

    AddMeshletCounterResetPass(renderGraphBuilder, resources, config);
    AddMeshletInstanceCullPass(renderGraphBuilder, resources, config);
    AddMeshletCandidateExpandPass(renderGraphBuilder, resources, config);
    AddMeshletFineCullPass(renderGraphBuilder, resources, config);
    if (config.FrameState->DebugMeshletClusters)
    {
        AddMeshletStatisticsReadbackPass(renderGraphBuilder, resources, config);
    }
    if (config.FrameState->UseTaskShaderMeshlets &&
        resources.GBufferTaskMeshShader != nullptr)
    {
        AddMeshletTaskGBufferPass(renderGraphBuilder, resources, config);
    }
    else
    {
        AddMeshletIndirectGBufferPass(renderGraphBuilder, resources, config);
    }
}
//Modify End
