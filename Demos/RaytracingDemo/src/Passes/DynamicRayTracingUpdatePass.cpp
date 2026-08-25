//Modify Begin:2026-08-25 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/IndexBuffer.h>
#include <DX12Library/VertexBuffer.h>
#include <Framework/Diagnostics/DiagnosticsSession.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <RenderGraph/RenderGraphBuilder.h>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct DynamicRayTracingUpdatePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };
}

void RaytracingDemoPasses::Builder::AddDynamicRayTracingUpdatePasses(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources)
{
    const VertexBuffer& dynamicVertexBuffer = resources.Scene.GetDynamicRayTracingVertexBuffer();
    const IndexBuffer& dynamicIndexBuffer = resources.Scene.GetDynamicRayTracingIndexBuffer();
    const MeshletGpuResources meshletGpuResources = resources.Scene.GetMeshletGpuResources();
    Assert(meshletGpuResources.IsValid(), "Dynamic RTAS updates require initialized meshlet resources.");
    renderGraphBuilder.AddPass<DynamicRayTracingUpdatePassData>(
        L"Dynamic RTAS Geometry Upload",
        [&resources, &dynamicVertexBuffer, meshletGpuResources](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            DynamicRayTracingUpdatePassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.WriteExternal(dynamicVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteExternal(*meshletGpuResources.Vertices, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteExternal(*meshletGpuResources.Meshlets, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteExternal(*meshletGpuResources.Transforms, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteExternal(*meshletGpuResources.Instances, D3D12_RESOURCE_STATE_COPY_DEST);
            passBuilder.WriteToken(DemoResourceIds::DynamicRayTracingGeometryUploadedToken);
        },
        [](const DynamicRayTracingUpdatePassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            const bool updated = passData.Resources->Scene.BeginDynamicRayTracingGeometryUpdate(
                commandList,
                context.GetMetadata().m_Time);
            Assert(updated, "Dynamic RTAS geometry upload pass was scheduled without pending scene work.");
            passData.Resources->Scene.RefreshDynamicEmissiveMeshSurfaceEmitters(passData.Resources->Lights);
        });

    renderGraphBuilder.AddPass<DynamicRayTracingUpdatePassData>(
        L"Dynamic RTAS Refit",
        [&resources, &dynamicVertexBuffer, &dynamicIndexBuffer](
            RenderGraph::RenderGraphPassBuilder& passBuilder,
            DynamicRayTracingUpdatePassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(DemoResourceIds::DynamicRayTracingGeometryUploadedToken);
            passBuilder.ReadExternal(dynamicVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.ReadExternal(dynamicIndexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.WriteToken(DemoResourceIds::DynamicRayTracingUpdatedToken);
        },
        [](const DynamicRayTracingUpdatePassData& passData, const RenderGraph::RenderContext&, CommandList& commandList)
        {
            RaytracingDemoSceneResources& scene = passData.Resources->Scene;
            const bool updated = scene.FinishDynamicRayTracingUpdate(commandList);
            Assert(updated, "Dynamic RTAS refit pass ran before the geometry upload pass.");

            FrameworkDiagnostics::DiagnosticsSession* diagnostics = passData.Resources->Diagnostics;
            if (diagnostics == nullptr || !diagnostics->IsEnabled())
            {
                return;
            }

            const RayTracingAccelerationStructureUpdateStatistics& accelerationStats =
                scene.GetRayTracingAccelerationStructure().GetUpdateStatistics();
            const RaytracingDemoDynamicRtasUpdateStatistics& sceneStats =
                scene.GetDynamicRayTracingUpdateStatistics();
            diagnostics->Record(
                "raytracing.rtas",
                "dynamic_refit",
                DiagnosticTelemetrySeverity::Info,
                {
                    { "geometry_upload_count", sceneStats.GeometryUploadCount },
                    { "meshlet_transform_update_count", sceneStats.MeshletTransformUpdateCount },
                    { "meshlet_geometry_update_count", sceneStats.MeshletGeometryUpdateCount },
                    { "emissive_mesh_refresh_count", sceneStats.EmissiveMeshRefreshCount },
                    { "refit_count", sceneStats.RefitCount },
                    { "restore_count", sceneStats.RestoreCount },
                    { "last_update_restored", sceneStats.LastUpdateRestored },
                    { "blas_update_count", accelerationStats.BottomLevelUpdateCount },
                    { "tlas_update_count", accelerationStats.TopLevelUpdateCount },
                    { "tlas_build_count", accelerationStats.TopLevelBuildCount },
                    { "retired_resource_count", accelerationStats.RetiredResourceCount },
                    { "tlas_gpu_virtual_address", scene.GetRayTracingAccelerationStructure().GetGpuVirtualAddress() },
                });
        });
}
//Modify End
