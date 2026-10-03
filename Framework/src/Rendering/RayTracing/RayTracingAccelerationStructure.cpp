//Modify Begin:2026-10-02 by Hui
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandListInternalAccess.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Geometry/Mesh.h>
#include <DX12Library/PerformanceScope.h>

#include <d3dx12/d3dx12.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

RayTracingAccelerationStructure::RayTracingAccelerationStructure(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_DeviceContext(std::move(deviceContext))
{
    Assert(m_DeviceContext != nullptr, "Ray tracing acceleration structure requires a D3D12 device context.");
    ThrowIfFailed(m_DeviceContext->GetDevice().As(&m_Device));
    Assert(m_Device != nullptr, "Ray tracing acceleration structure requires a DXR-capable device.");
}

RayTracingInstanceHandle RayTracingAccelerationStructure::AddInstance(const RayTracingInstanceDesc& instanceDesc)
{
    Assert(instanceDesc.Mesh != nullptr, "Ray tracing instance mesh must not be null.");
    const RayTracingInstanceHandle handle = m_NextInstanceHandle++;
    const uint32_t instanceIndex = static_cast<uint32_t>(m_Instances.size());
    m_InstanceHandles.push_back(handle);
    m_Instances.push_back(instanceDesc);
    m_InstanceIndices.emplace(handle, instanceIndex);
    return handle;
}

bool RayTracingAccelerationStructure::UpdateInstance(const RayTracingInstanceHandle handle, const RayTracingInstanceDesc& instanceDesc)
{
    Assert(instanceDesc.Mesh != nullptr, "Ray tracing instance mesh must not be null.");
    const auto indexResult = m_InstanceIndices.find(handle);
    if (indexResult == m_InstanceIndices.end())
    {
        return false;
    }

    const uint32_t instanceIndex = indexResult->second;
    m_InstanceMeshChanged |= m_Instances[instanceIndex].Mesh != instanceDesc.Mesh;
    m_Instances[instanceIndex] = instanceDesc;
    return true;
}

void RayTracingAccelerationStructure::MarkBottomLevelGeometryDirty(
    const std::span<const std::shared_ptr<Mesh>> meshes)
{
    for (const std::shared_ptr<Mesh>& mesh : meshes)
    {
        Assert(mesh != nullptr, "Ray tracing dirty BLAS mesh must not be null.");
        m_DirtyBottomLevelMeshes.emplace(mesh.get());
    }
}

bool RayTracingAccelerationStructure::RemoveInstance(const RayTracingInstanceHandle handle)
{
    const auto indexResult = m_InstanceIndices.find(handle);
    if (indexResult == m_InstanceIndices.end())
    {
        return false;
    }

    const uint32_t instanceIndex = indexResult->second;
    const uint32_t lastIndex = static_cast<uint32_t>(m_InstanceHandles.size() - 1);
    if (instanceIndex != lastIndex)
    {
        const RayTracingInstanceHandle movedHandle = m_InstanceHandles[lastIndex];
        m_InstanceHandles[instanceIndex] = movedHandle;
        m_Instances[instanceIndex] = std::move(m_Instances[lastIndex]);
        m_InstanceIndices[movedHandle] = instanceIndex;
    }

    m_InstanceHandles.pop_back();
    m_Instances.pop_back();
    // Updating the moved instance may rehash the unordered map and invalidate indexResult.
    m_InstanceIndices.erase(handle);
    return true;
}

void RayTracingAccelerationStructure::RemoveInstances(const std::span<const RayTracingInstanceHandle> handles)
{
    if (handles.empty())
    {
        return;
    }

    std::unordered_set<RayTracingInstanceHandle> handlesToRemove(handles.begin(), handles.end());
    Assert(handlesToRemove.size() == handles.size(), "Ray tracing instance removal contains duplicate handles.");
    Assert(handlesToRemove.size() <= m_Instances.size(), "Ray tracing instance removal exceeds the instance count.");

    for (const RayTracingInstanceHandle handle : handlesToRemove)
    {
        Assert(m_InstanceIndices.contains(handle), "Ray tracing instance handle is invalid.");
    }

    std::vector<RayTracingInstanceHandle> survivingHandles;
    std::vector<RayTracingInstanceDesc> survivingInstances;
    survivingHandles.reserve(m_Instances.size() - handlesToRemove.size());
    survivingInstances.reserve(m_Instances.size() - handlesToRemove.size());
    for (uint32_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
    {
        if (!handlesToRemove.contains(m_InstanceHandles[instanceIndex]))
        {
            survivingHandles.push_back(m_InstanceHandles[instanceIndex]);
            survivingInstances.push_back(std::move(m_Instances[instanceIndex]));
        }
    }

    m_InstanceHandles = std::move(survivingHandles);
    m_Instances = std::move(survivingInstances);
    m_InstanceIndices.clear();
    m_InstanceIndices.reserve(m_InstanceHandles.size());
    for (uint32_t instanceIndex = 0; instanceIndex < m_InstanceHandles.size(); ++instanceIndex)
    {
        const bool inserted = m_InstanceIndices.emplace(m_InstanceHandles[instanceIndex], instanceIndex).second;
        Assert(inserted, "Ray tracing instance handle is duplicated.");
    }
}

void RayTracingAccelerationStructure::ClearInstances()
{
    m_InstanceHandles.clear();
    m_Instances.clear();
    m_InstanceIndices.clear();
    m_InstanceMeshChanged = false;
}

void RayTracingAccelerationStructure::Build(CommandList& commandList, RayTracingAccelerationStructureBuildSettings settings)
{
    Assert(!m_Instances.empty(), "Ray tracing acceleration structure requires at least one instance.");

    ++m_UpdateStatistics.FullBuildCount;
    m_LastBuildSettings = settings;
    if (settings.AllowUpdate)
    {
        settings.BottomLevelFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        settings.TopLevelFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        m_LastBuildSettings = settings;
    }

    for (const BottomLevelAccelerationStructure& bottomLevel : m_BottomLevelAccelerationStructures)
    {
        RetireResourceState(commandList, bottomLevel.Resource.Resource);
    }
    if (m_TopLevelAccelerationStructure)
    {
        RetireResourceState(commandList, m_TopLevelAccelerationStructure.Resource);
    }
    m_BottomLevelAccelerationStructures.clear();
    m_TopLevelAccelerationStructure = {};
    m_Meshes.clear();
    m_GeometryData.clear();
    m_DirtyBottomLevelMeshes.clear();

    const std::map<const Mesh*, uint32_t> meshToBlasIndex = BuildBottomLevelAccelerationStructures(commandList);
    BuildTopLevelAccelerationStructure(commandList, meshToBlasIndex, false, nullptr, 0u);
    m_BuiltInstanceCount = static_cast<uint32_t>(m_Instances.size());
    m_InstanceMeshChanged = false;
}

void RayTracingAccelerationStructure::Build(CommandList& commandList, const std::vector<RayTracingMeshInstance>& instances)
{
    ClearInstances();
    for (const RayTracingMeshInstance& instance : instances)
    {
        AddInstance({
            instance.Mesh,
            instance.Transform,
            instance.MaterialIndex
        });
    }

    Build(commandList);
}

void RayTracingAccelerationStructure::Update(
    CommandList& commandList,
    DiagnosticTelemetrySink* diagnostics,
    const uint64_t frameIndex)
{
    if (!m_LastBuildSettings.AllowUpdate || !m_TopLevelAccelerationStructure)
    {
        Build(commandList, m_LastBuildSettings);
        return;
    }

    Assert(!m_Instances.empty(), "Ray tracing acceleration structure requires at least one instance.");
    m_GeometryData.clear();
    const size_t previousBottomLevelCount = m_BottomLevelAccelerationStructures.size();
    std::map<const Mesh*, uint32_t> meshToBlasIndex;
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.BuildBottomLevelSet",
            "CPU",
            0u,
            "framework");
        meshToBlasIndex = BuildBottomLevelAccelerationStructures(commandList);
    }
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.UpdateDirtyBottomLevels",
            "CPU",
            0u,
            "framework");
        UpdateDirtyBottomLevelAccelerationStructures(commandList, diagnostics, frameIndex);
    }
    const bool instanceCountChanged = m_BuiltInstanceCount != m_Instances.size();
    const bool bottomLevelSetChanged = previousBottomLevelCount != m_BottomLevelAccelerationStructures.size();
    const bool canPerformUpdate = !instanceCountChanged && !bottomLevelSetChanged && !m_InstanceMeshChanged;
    if (!canPerformUpdate)
    {
        RetireResourceState(commandList, m_TopLevelAccelerationStructure.Resource);
        m_TopLevelAccelerationStructure = {};
    }

    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            canPerformUpdate ? "RTAS.BuildTopLevelUpdate" : "RTAS.BuildTopLevelRebuild",
            "CPU",
            0u,
            "framework");
        BuildTopLevelAccelerationStructure(
            commandList,
            meshToBlasIndex,
            canPerformUpdate,
            diagnostics,
            frameIndex);
    }
    m_BuiltInstanceCount = static_cast<uint32_t>(m_Instances.size());
    m_InstanceMeshChanged = false;
}

bool RayTracingAccelerationStructure::IsBuilt() const
{
    return static_cast<bool>(m_TopLevelAccelerationStructure);
}

D3D12_GPU_VIRTUAL_ADDRESS RayTracingAccelerationStructure::GetGpuVirtualAddress() const
{
    return m_TopLevelAccelerationStructure.Resource->GetGPUVirtualAddress();
}

ID3D12Resource* RayTracingAccelerationStructure::GetResource() const
{
    return m_TopLevelAccelerationStructure.Resource.Get();
}

const std::vector<std::shared_ptr<Mesh>>& RayTracingAccelerationStructure::GetMeshes() const
{
    return m_Meshes;
}

const std::vector<RayTracingGeometryData>& RayTracingAccelerationStructure::GetGeometryData() const
{
    return m_GeometryData;
}

const std::vector<RayTracingInstanceDesc>& RayTracingAccelerationStructure::GetInstances() const
{
    return m_Instances;
}

uint32_t RayTracingAccelerationStructure::GetInstanceCount() const
{
    return static_cast<uint32_t>(m_Instances.size());
}

RayTracingAccelerationStructure::ManagedRayTracingResource
RayTracingAccelerationStructure::CreateAccelerationStructureBuffer(
    const uint64_t size,
    const D3D12_RESOURCE_STATES initialState,
    const wchar_t* name) const
{
    const auto& device = m_Device;
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    if (name != nullptr)
    {
        resource->SetName(name);
    }

    return {
        resource,
        m_DeviceContext->GetResourceStateRegistry()->AcquireResource(resource.Get(), initialState)
    };
}

RayTracingAccelerationStructure::ManagedRayTracingResource RayTracingAccelerationStructure::CreateUploadBuffer(
    const void* data,
    const uint64_t size,
    const wchar_t* name) const
{
    const auto& device = m_Device;
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource)));

    if (name != nullptr)
    {
        resource->SetName(name);
    }

    void* mappedData = nullptr;
    const D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(resource->Map(0, &readRange, &mappedData));
    std::memcpy(mappedData, data, size);
    const D3D12_RANGE writeRange = { 0, static_cast<SIZE_T>(size) };
    resource->Unmap(0, &writeRange);

    return {
        resource,
        m_DeviceContext->GetResourceStateRegistry()->AcquireResource(
            resource.Get(),
            D3D12_RESOURCE_STATE_GENERIC_READ)
    };
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO RayTracingAccelerationStructure::GetBottomLevelPrebuildInfo(
    const Mesh& mesh,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags) const
{
    const VertexBuffer& vertexBuffer = mesh.GetVertexBuffer();
    const IndexBuffer& indexBuffer = mesh.GetIndexBuffer();

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.GetVertexStride();
    geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.GetNumVertices());
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.IndexBuffer = indexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.GetNumIndices());
    geometryDesc.Triangles.IndexFormat = indexBuffer.GetIndexFormat();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = buildFlags;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometryDesc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    m_Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid BLAS prebuild info.");
    return prebuildInfo;
}

RayTracingAccelerationStructure::BottomLevelAccelerationStructure RayTracingAccelerationStructure::BuildBottomLevelAccelerationStructure(
    CommandList& commandList,
    const std::shared_ptr<Mesh>& mesh,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags,
    const ManagedRayTracingResource& scratch)
{
    const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
    const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();
    CommandContext commandContext(commandList);

    commandContext.UseResource(vertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, ResourceUse::Read);
    commandContext.UseResource(indexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, ResourceUse::Read);

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.GetVertexStride();
    geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.GetNumVertices());
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.IndexBuffer = indexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.GetNumIndices());
    geometryDesc.Triangles.IndexFormat = indexBuffer.GetIndexFormat();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = buildFlags;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometryDesc;

    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo =
        GetBottomLevelPrebuildInfo(*mesh, buildFlags);
    Assert(scratch.Resource != nullptr, "BLAS scratch buffer must not be null.");
    Assert(
        scratch.Resource->GetDesc().Width >= prebuildInfo.ScratchDataSizeInBytes,
        "BLAS scratch buffer is smaller than the required prebuild size.");
    commandContext.UseResource(
        scratch.Resource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        ResourceUse::Write);

    auto result = CreateAccelerationStructureBuffer(
        prebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        L"Ray Tracing Bottom Level Acceleration Structure");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = result.Resource->GetGPUVirtualAddress();

    commandList.BuildRaytracingAccelerationStructure(buildDesc);
    commandContext.UavBarrier(result.Resource.Get());
    commandContext.UavBarrier(scratch.Resource.Get());
    CommandListInternalAccess::TrackResourceState(commandList, result.Resource, result.StateRegistration);

    ++m_UpdateStatistics.BottomLevelBuildCount;
    return { mesh, result };
}

std::map<const Mesh*, uint32_t> RayTracingAccelerationStructure::BuildBottomLevelAccelerationStructures(CommandList& commandList)
{
    std::map<const Mesh*, uint32_t> meshToBlasIndex = CreateMeshToBlasIndex();
    std::map<const Mesh*, std::shared_ptr<Mesh>> meshesToBuild;

    for (const RayTracingInstanceDesc& instance : m_Instances)
    {
        if (meshToBlasIndex.contains(instance.Mesh.get()))
        {
            continue;
        }

        meshesToBuild.emplace(instance.Mesh.get(), instance.Mesh);
    }

    uint64_t scratchBufferSize = 0;
    for (const auto& [meshPointer, mesh] : meshesToBuild)
    {
        static_cast<void>(meshPointer);
        scratchBufferSize = (std::max)(
            scratchBufferSize,
            GetBottomLevelPrebuildInfo(*mesh, m_LastBuildSettings.BottomLevelFlags).ScratchDataSizeInBytes);
    }

    ManagedRayTracingResource scratch;
    if (scratchBufferSize > 0)
    {
        const auto transientScratch = CommandListInternalAccess::GetOrCreateTransientUavBuffer(
            commandList,
            reinterpret_cast<uint64_t>(this) ^ 0x424C41535F425549ull,
            scratchBufferSize,
            L"Ray Tracing BLAS Scratch");
        scratch = { transientScratch.Resource, transientScratch.StateRegistration };
        CommandListInternalAccess::TrackObjectLifetime(commandList, scratch.Resource);
    }

    for (const auto& [meshPointer, mesh] : meshesToBuild)
    {
        const uint32_t meshIndex = static_cast<uint32_t>(m_BottomLevelAccelerationStructures.size());
        meshToBlasIndex.emplace(meshPointer, meshIndex);
        m_BottomLevelAccelerationStructures.push_back(
            BuildBottomLevelAccelerationStructure(commandList, mesh, m_LastBuildSettings.BottomLevelFlags, scratch));
        m_Meshes.push_back(mesh);
    }

    return meshToBlasIndex;
}

void RayTracingAccelerationStructure::UpdateDirtyBottomLevelAccelerationStructures(
    CommandList& commandList,
    DiagnosticTelemetrySink* diagnostics,
    const uint64_t frameIndex)
{
    if (m_DirtyBottomLevelMeshes.empty())
    {
        return;
    }

    uint64_t scratchBufferSize = 0;
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.UpdateDirtyBottomLevels.Prebuild",
            "CPU",
            0u,
            "framework");
        std::unordered_set<const Mesh*> knownMeshes;
        for (const BottomLevelAccelerationStructure& bottomLevel : m_BottomLevelAccelerationStructures)
        {
            const Mesh* mesh = bottomLevel.Mesh.get();
            knownMeshes.emplace(mesh);
            if (!m_DirtyBottomLevelMeshes.contains(mesh))
            {
                continue;
            }

            scratchBufferSize = (std::max)(
                scratchBufferSize,
                GetBottomLevelPrebuildInfo(
                    *bottomLevel.Mesh,
                    m_LastBuildSettings.BottomLevelFlags |
                        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE)
                    .ScratchDataSizeInBytes);
        }

        for (const Mesh* mesh : m_DirtyBottomLevelMeshes)
        {
            Assert(knownMeshes.contains(mesh), "Ray tracing dirty BLAS mesh is not part of the current acceleration structure.");
        }
    }

    Assert(scratchBufferSize > 0, "Ray tracing BLAS update requires non-zero scratch storage.");
    ManagedRayTracingResource scratch;
    {
        const uint64_t scratchKey = reinterpret_cast<uint64_t>(this) ^ 0x424C41535F555044ull;
        const bool scratchCacheMiss = !CommandListInternalAccess::HasTransientUavBuffer(
            commandList,
            scratchKey,
            scratchBufferSize);
        DX12Diagnostics::ScopedCpuPerformanceScope scratchScope(
            scratchCacheMiss ? diagnostics : nullptr,
            frameIndex,
            "RTAS.UpdateDirtyBottomLevels.CreateScratch",
            "CPU",
            0u,
            "framework");
        const auto transientScratch = CommandListInternalAccess::GetOrCreateTransientUavBuffer(
            commandList,
            scratchKey,
            scratchBufferSize,
            L"Ray Tracing BLAS Update Scratch");
        scratch = { transientScratch.Resource, transientScratch.StateRegistration };
        CommandListInternalAccess::TrackObjectLifetime(commandList, scratch.Resource);
    }
    CommandContext commandContext(commandList);
    CommandListInternalAccess::TrackResourceState(commandList, scratch.Resource, scratch.StateRegistration);
    commandContext.UseResource(
        scratch.Resource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        ResourceUse::Write);

    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.UpdateDirtyBottomLevels.RecordBuilds",
            "CPU",
            0u,
            "framework");
        for (BottomLevelAccelerationStructure& bottomLevel : m_BottomLevelAccelerationStructures)
        {
            if (!m_DirtyBottomLevelMeshes.contains(bottomLevel.Mesh.get()))
            {
                continue;
            }

            const VertexBuffer& vertexBuffer = bottomLevel.Mesh->GetVertexBuffer();
            const IndexBuffer& indexBuffer = bottomLevel.Mesh->GetIndexBuffer();
            D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
            geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
            geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.GetVertexStride();
            geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.GetNumVertices());
            geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometryDesc.Triangles.IndexBuffer = indexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
            geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.GetNumIndices());
            geometryDesc.Triangles.IndexFormat = indexBuffer.GetIndexFormat();

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags = m_LastBuildSettings.BottomLevelFlags |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = &geometryDesc;

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
            buildDesc.Inputs = inputs;
            buildDesc.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress();
            buildDesc.SourceAccelerationStructureData = bottomLevel.Resource.Resource->GetGPUVirtualAddress();
            buildDesc.DestAccelerationStructureData = bottomLevel.Resource.Resource->GetGPUVirtualAddress();
            commandList.BuildRaytracingAccelerationStructure(buildDesc);
            commandContext.UavBarrier(bottomLevel.Resource.Resource.Get());
            commandContext.UavBarrier(scratch.Resource.Get());
            ++m_UpdateStatistics.BottomLevelUpdateCount;
        }
    }

    m_DirtyBottomLevelMeshes.clear();
}

std::map<const Mesh*, uint32_t> RayTracingAccelerationStructure::CreateMeshToBlasIndex() const
{
    std::map<const Mesh*, uint32_t> meshToBlasIndex;
    for (uint32_t i = 0; i < m_BottomLevelAccelerationStructures.size(); ++i)
    {
        meshToBlasIndex.emplace(m_BottomLevelAccelerationStructures[i].Mesh.get(), i);
    }
    return meshToBlasIndex;
}

void RayTracingAccelerationStructure::BuildTopLevelAccelerationStructure(
    CommandList& commandList,
    const std::map<const Mesh*, uint32_t>& meshToBlasIndex,
    const bool update,
    DiagnosticTelemetrySink* diagnostics,
    const uint64_t frameIndex)
{
    const auto& device = m_Device;

    m_InstanceDescs.clear();
    m_InstanceDescs.reserve(m_Instances.size());
    m_GeometryData.reserve(m_Instances.size());

    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.BuildTopLevelUpdate.BuildInstanceDescs",
            "CPU",
            0u,
            "framework");
        for (uint32_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const RayTracingInstanceDesc& instance = m_Instances[instanceIndex];
            const uint32_t blasIndex = meshToBlasIndex.at(instance.Mesh.get());
            const uint32_t instanceID = instance.InstanceID == UINT32_MAX ? instanceIndex : instance.InstanceID;

            D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
            DirectX::XMStoreFloat3x4(reinterpret_cast<DirectX::XMFLOAT3X4*>(instanceDesc.Transform), instance.Transform);
            instanceDesc.InstanceID = instanceID;
            instanceDesc.InstanceMask = instance.Mask;
            instanceDesc.InstanceContributionToHitGroupIndex = instance.InstanceContributionToHitGroupIndex;
            instanceDesc.Flags = instance.Flags;
            instanceDesc.AccelerationStructure =
                m_BottomLevelAccelerationStructures[blasIndex].Resource.Resource->GetGPUVirtualAddress();
            m_InstanceDescs.push_back(instanceDesc);

            m_GeometryData.push_back({
                blasIndex,
                blasIndex,
                instance.MaterialIndex,
                0
            });
        }
    }

    const size_t instanceDescSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_InstanceDescs.size();
    UploadBuffer::Allocation instanceDescUpload;
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.BuildTopLevelUpdate.UploadInstanceDescs",
            "CPU",
            0u,
            "framework");
        instanceDescUpload = CommandListInternalAccess::AllocateTransientUpload(
            commandList,
            instanceDescSize,
            16u);
        std::memcpy(instanceDescUpload.Cpu, m_InstanceDescs.data(), instanceDescSize);
        CommandListInternalAccess::TrackObjectLifetime(commandList, instanceDescUpload.Resource);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = m_LastBuildSettings.TopLevelFlags;
    inputs.NumDescs = static_cast<UINT>(m_InstanceDescs.size());
    inputs.InstanceDescs = instanceDescUpload.Gpu;

    if (update)
    {
        inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            diagnostics,
            frameIndex,
            "RTAS.BuildTopLevelUpdate.Prebuild",
            "CPU",
            0u,
            "framework");
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
        Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid TLAS prebuild info.");
    }

    if (!update)
    {
        if (m_TopLevelAccelerationStructure)
        {
            RetireResourceState(commandList, m_TopLevelAccelerationStructure.Resource);
        }
        m_TopLevelAccelerationStructure = CreateAccelerationStructureBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            L"Ray Tracing Top Level Acceleration Structure");
    }

    ManagedRayTracingResource scratch;
    {
        const uint64_t scratchKey = reinterpret_cast<uint64_t>(this) ^ 0x544C41535F534352ull;
        const bool scratchCacheMiss = !CommandListInternalAccess::HasTransientUavBuffer(
            commandList,
            scratchKey,
            prebuildInfo.ScratchDataSizeInBytes);
        DX12Diagnostics::ScopedCpuPerformanceScope scratchScope(
            scratchCacheMiss ? diagnostics : nullptr,
            frameIndex,
            "RTAS.BuildTopLevelUpdate.CreateScratch",
            "CPU",
            0u,
            "framework");
        const auto transientScratch = CommandListInternalAccess::GetOrCreateTransientUavBuffer(
            commandList,
            scratchKey,
            prebuildInfo.ScratchDataSizeInBytes,
            update ? L"Ray Tracing TLAS Update Scratch" : L"Ray Tracing TLAS Scratch");
        scratch = { transientScratch.Resource, transientScratch.StateRegistration };
        CommandListInternalAccess::TrackObjectLifetime(commandList, scratch.Resource);
    }
    CommandContext commandContext(commandList);

    commandContext.UseResource(scratch.Resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ResourceUse::Write);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch.Resource->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure.Resource->GetGPUVirtualAddress();
    buildDesc.SourceAccelerationStructureData =
        update ? m_TopLevelAccelerationStructure.Resource->GetGPUVirtualAddress() : 0;

    commandList.BuildRaytracingAccelerationStructure(buildDesc);
    commandContext.UavBarrier(m_TopLevelAccelerationStructure.Resource.Get());
    CommandListInternalAccess::TrackResourceState(commandList, scratch.Resource, scratch.StateRegistration);
    CommandListInternalAccess::TrackResourceState(
        commandList,
        m_TopLevelAccelerationStructure.Resource,
        m_TopLevelAccelerationStructure.StateRegistration);
    if (update)
    {
        ++m_UpdateStatistics.TopLevelUpdateCount;
    }
    else
    {
        ++m_UpdateStatistics.TopLevelBuildCount;
    }
}

void RayTracingAccelerationStructure::RetireResourceState(
    CommandList& commandList,
    const ComPtr<ID3D12Resource>& resource)
{
    CommandListInternalAccess::RetireResourceState(commandList, resource);
    ++m_UpdateStatistics.RetiredResourceCount;
}
//Modify End
