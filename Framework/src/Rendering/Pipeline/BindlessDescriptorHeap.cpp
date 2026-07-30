//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>

BindlessDescriptorHeap::BindlessDescriptorHeap(BindlessDescriptorHeapDesc desc)
    : m_Desc(desc)
{
    const auto device = Application::Get().GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = m_Desc.ResourceDescriptorCapacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_ResourceDescriptorHeap)));
    ThrowIfFailed(m_ResourceDescriptorHeap->SetName(L"Framework Bindless Resource Descriptor Heap"));

    m_ResourceDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void BindlessDescriptorHeap::Reset()
{
    m_NextResourceDescriptorIndex = 0;
    m_CachedDescriptorTables.clear();
}

uint32_t BindlessDescriptorHeap::AddShaderResourceView(
    const Resource& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
{
    Assert(
        m_NextResourceDescriptorIndex < m_Desc.ResourceDescriptorCapacity,
        "Bindless resource descriptor heap capacity was exceeded.");

    const uint32_t descriptorIndex = m_NextResourceDescriptorIndex++;
    UpdateShaderResourceView(descriptorIndex, resource, srvDesc);
    return descriptorIndex;
}

void BindlessDescriptorHeap::UpdateShaderResourceView(
    const uint32_t descriptorIndex,
    const Resource& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    Application::Get().GetDevice()->CopyDescriptorsSimple(
        1u,
        GetResourceCpuHandle(descriptorIndex),
        resource.GetShaderResourceView(srvDesc),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetOrCreateDescriptorTable(
    const PipelineDescriptorTableAllocation& allocation)
{
    Assert(allocation.IsValid(), "Bindless descriptor table allocation is invalid.");

    const uint64_t cacheKey =
        allocation.GetDescriptorHandle().ptr ^
        (static_cast<uint64_t>(allocation.GetNumHandles()) << 32u);
    auto cacheIt = m_CachedDescriptorTables.find(cacheKey);
    if (cacheIt == m_CachedDescriptorTables.end())
    {
        Assert(
            m_NextResourceDescriptorIndex + allocation.GetNumHandles() <= m_Desc.ResourceDescriptorCapacity,
            "Bindless descriptor heap capacity was exceeded by descriptor tables.");

        CachedDescriptorTable cachedTable;
        cachedTable.DescriptorIndex = m_NextResourceDescriptorIndex;
        cachedTable.DescriptorCount = allocation.GetNumHandles();
        cachedTable.Revision = 0;
        m_NextResourceDescriptorIndex += allocation.GetNumHandles();
        cacheIt = m_CachedDescriptorTables.emplace(cacheKey, cachedTable).first;
    }

    CachedDescriptorTable& cachedTable = cacheIt->second;
    Assert(cachedTable.DescriptorCount == allocation.GetNumHandles(), "Bindless descriptor table cache count mismatch.");
    if (cachedTable.Revision != allocation.GetRevision())
    {
        Application::Get().GetDevice()->CopyDescriptorsSimple(
            allocation.GetNumHandles(),
            GetResourceCpuHandle(cachedTable.DescriptorIndex),
            allocation.GetDescriptorHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cachedTable.Revision = allocation.GetRevision();
    }

    return GetResourceGpuHandle(cachedTable.DescriptorIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetResourceGpuHandle(const uint32_t descriptorIndex) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_ResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_ResourceDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetResourceCpuHandle(const uint32_t descriptorIndex) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_ResourceDescriptorSize;
    return handle;
}
//Modify End
