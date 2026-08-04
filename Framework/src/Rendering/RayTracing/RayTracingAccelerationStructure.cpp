//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Geometry/Mesh.h>

#include <d3dx12.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

RayTracingAccelerationStructure::RayTracingAccelerationStructure(ComPtr<ID3D12Device5> device)
    : m_Device(std::move(device))
{
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
    m_InstanceIndices.erase(indexResult);
    return true;
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

    m_LastBuildSettings = settings;
    if (settings.AllowUpdate)
    {
        settings.BottomLevelFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        settings.TopLevelFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        m_LastBuildSettings = settings;
    }

    for (const BottomLevelAccelerationStructure& bottomLevel : m_BottomLevelAccelerationStructures)
    {
        commandList.TrackObject(bottomLevel.Resource);
    }
    if (m_TopLevelAccelerationStructure != nullptr)
    {
        commandList.TrackObject(m_TopLevelAccelerationStructure);
    }
    if (m_InstanceDescUpload != nullptr)
    {
        commandList.TrackObject(m_InstanceDescUpload);
    }
    m_BottomLevelAccelerationStructures.clear();
    m_TopLevelAccelerationStructure.Reset();
    m_InstanceDescUpload.Reset();
    m_Meshes.clear();
    m_GeometryData.clear();

    const std::map<const Mesh*, uint32_t> meshToBlasIndex = BuildBottomLevelAccelerationStructures(commandList);
    BuildTopLevelAccelerationStructure(commandList, meshToBlasIndex, false);
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

void RayTracingAccelerationStructure::Update(CommandList& commandList)
{
    if (!m_LastBuildSettings.AllowUpdate || m_TopLevelAccelerationStructure == nullptr)
    {
        Build(commandList, m_LastBuildSettings);
        return;
    }

    Assert(!m_Instances.empty(), "Ray tracing acceleration structure requires at least one instance.");
    m_InstanceDescUpload.Reset();
    m_GeometryData.clear();
    const size_t previousBottomLevelCount = m_BottomLevelAccelerationStructures.size();
    const std::map<const Mesh*, uint32_t> meshToBlasIndex = BuildBottomLevelAccelerationStructures(commandList);
    const bool instanceCountChanged = m_BuiltInstanceCount != m_Instances.size();
    const bool bottomLevelSetChanged = previousBottomLevelCount != m_BottomLevelAccelerationStructures.size();
    const bool canPerformUpdate = !instanceCountChanged && !bottomLevelSetChanged && !m_InstanceMeshChanged;
    if (!canPerformUpdate)
    {
        commandList.TrackObject(m_TopLevelAccelerationStructure);
        m_TopLevelAccelerationStructure.Reset();
    }

    BuildTopLevelAccelerationStructure(commandList, meshToBlasIndex, canPerformUpdate);
    m_BuiltInstanceCount = static_cast<uint32_t>(m_Instances.size());
    m_InstanceMeshChanged = false;
}

bool RayTracingAccelerationStructure::IsBuilt() const
{
    return m_TopLevelAccelerationStructure != nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS RayTracingAccelerationStructure::GetGpuVirtualAddress() const
{
    return m_TopLevelAccelerationStructure->GetGPUVirtualAddress();
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

ComPtr<ID3D12Resource> RayTracingAccelerationStructure::CreateAccelerationStructureBuffer(
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

    return resource;
}

ComPtr<ID3D12Resource> RayTracingAccelerationStructure::CreateUploadBuffer(
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
    ThrowIfFailed(resource->Map(0, nullptr, &mappedData));
    std::memcpy(mappedData, data, size);
    resource->Unmap(0, nullptr);

    return resource;
}

RayTracingAccelerationStructure::BottomLevelAccelerationStructure RayTracingAccelerationStructure::BuildBottomLevelAccelerationStructure(
    CommandList& commandList,
    const std::shared_ptr<Mesh>& mesh,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags) const
{
    const auto& device = m_Device;
    const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
    const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();

    commandList.TransitionBarrier(vertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(indexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid BLAS prebuild info.");

    auto result = CreateAccelerationStructureBuffer(
        prebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        L"Ray Tracing Bottom Level Acceleration Structure");
    auto scratch = CreateAccelerationStructureBuffer(
        prebuildInfo.ScratchDataSizeInBytes,
        D3D12_RESOURCE_STATE_COMMON,
        L"Ray Tracing BLAS Scratch");

    const D3D12_RESOURCE_BARRIER scratchBarrier =
        CD3DX12_RESOURCE_BARRIER::Transition(scratch.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList.GetGraphicsCommandList()->ResourceBarrier(1, &scratchBarrier);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = result->GetGPUVirtualAddress();

    commandList.BuildRaytracingAccelerationStructure(buildDesc);
    const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(result.Get());
    commandList.GetGraphicsCommandList()->ResourceBarrier(1, &uavBarrier);
    commandList.TrackObject(scratch);

    return { mesh, result };
}

std::map<const Mesh*, uint32_t> RayTracingAccelerationStructure::BuildBottomLevelAccelerationStructures(CommandList& commandList)
{
    std::map<const Mesh*, uint32_t> meshToBlasIndex = CreateMeshToBlasIndex();

    for (const RayTracingInstanceDesc& instance : m_Instances)
    {
        if (meshToBlasIndex.contains(instance.Mesh.get()))
        {
            continue;
        }

        const uint32_t meshIndex = static_cast<uint32_t>(m_BottomLevelAccelerationStructures.size());
        meshToBlasIndex.emplace(instance.Mesh.get(), meshIndex);
        m_BottomLevelAccelerationStructures.push_back(
            BuildBottomLevelAccelerationStructure(commandList, instance.Mesh, m_LastBuildSettings.BottomLevelFlags));
        m_Meshes.push_back(instance.Mesh);
    }

    return meshToBlasIndex;
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
    const bool update)
{
    const auto& device = m_Device;

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    instanceDescs.reserve(m_Instances.size());
    m_GeometryData.reserve(m_Instances.size());

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
        instanceDesc.AccelerationStructure = m_BottomLevelAccelerationStructures[blasIndex].Resource->GetGPUVirtualAddress();
        instanceDescs.push_back(instanceDesc);

        m_GeometryData.push_back({
            blasIndex,
            blasIndex,
            instance.MaterialIndex,
            0
        });
    }

    m_InstanceDescUpload = CreateUploadBuffer(
        instanceDescs.data(),
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescs.size(),
        L"Ray Tracing Instance Descriptions");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = m_LastBuildSettings.TopLevelFlags;
    inputs.NumDescs = static_cast<UINT>(instanceDescs.size());
    inputs.InstanceDescs = m_InstanceDescUpload->GetGPUVirtualAddress();

    if (update)
    {
        inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid TLAS prebuild info.");

    if (!update)
    {
        if (m_TopLevelAccelerationStructure != nullptr)
        {
            commandList.TrackObject(m_TopLevelAccelerationStructure);
        }
        m_TopLevelAccelerationStructure = CreateAccelerationStructureBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            L"Ray Tracing Top Level Acceleration Structure");
    }

    auto scratch = CreateAccelerationStructureBuffer(
        prebuildInfo.ScratchDataSizeInBytes,
        D3D12_RESOURCE_STATE_COMMON,
        update ? L"Ray Tracing TLAS Update Scratch" : L"Ray Tracing TLAS Scratch");

    const D3D12_RESOURCE_BARRIER scratchBarrier =
        CD3DX12_RESOURCE_BARRIER::Transition(scratch.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList.GetGraphicsCommandList()->ResourceBarrier(1, &scratchBarrier);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure->GetGPUVirtualAddress();
    buildDesc.SourceAccelerationStructureData = update ? m_TopLevelAccelerationStructure->GetGPUVirtualAddress() : 0;

    commandList.BuildRaytracingAccelerationStructure(buildDesc);
    const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_TopLevelAccelerationStructure.Get());
    commandList.GetGraphicsCommandList()->ResourceBarrier(1, &uavBarrier);
    commandList.TrackObject(scratch);
    commandList.TrackObject(m_InstanceDescUpload);
    commandList.TrackObject(m_TopLevelAccelerationStructure);
}
//Modify End
