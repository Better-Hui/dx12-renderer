//Modify Begin:2026-08-10 by Hui
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/Texture.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>

#include <string>

BindlessDescriptorHeap::BindlessDescriptorHeap(ID3D12Device2& device, BindlessDescriptorHeapDesc desc)
    : m_Desc(desc)
    , m_Device(&device)
{
    Assert(m_Desc.ResourceDescriptorCapacity > 0u, "Bindless resource descriptor capacity must be positive.");
    Assert(m_Desc.MaxFramePages > 0u, "Bindless descriptor heap requires at least one frame page.");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = m_Desc.ResourceDescriptorCapacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_CanonicalResourceDescriptorHeap)));
    ThrowIfFailed(m_CanonicalResourceDescriptorHeap->SetName(L"Framework Bindless Canonical Descriptor Heap"));

    m_ResourceDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_ResourceDescriptorRevisions.resize(m_Desc.ResourceDescriptorCapacity);
}

void BindlessDescriptorHeap::Reset()
{
    std::lock_guard lock(m_Mutex);
    Assert(!m_FrameActive, "Bindless descriptor heap cannot be reset while a frame is recording.");
    m_NextResourceDescriptorIndex = 0;
    m_DefaultShaderResourceDescriptors.clear();
    m_CachedDescriptorTables.clear();
    std::fill(m_ResourceDescriptorRevisions.begin(), m_ResourceDescriptorRevisions.end(), 0u);
    ++m_LayoutGeneration;
    if (m_LayoutGeneration == 0u)
    {
        ++m_LayoutGeneration;
    }
    m_CurrentPageIndex = InvalidPageIndex;
}

void BindlessDescriptorHeap::BeginFrame(
    CommandQueue& directCommandQueue,
    CommandQueue& asyncComputeCommandQueue)
{
    std::lock_guard lock(m_Mutex);
    Assert(!m_FrameActive, "Bindless descriptor heap frame scope is already active.");

    uint32_t selectedPageIndex = InvalidPageIndex;
    for (uint32_t pageIndex = 0u; pageIndex < m_Pages.size(); ++pageIndex)
    {
        if (IsPageAvailableLocked(m_Pages[pageIndex], directCommandQueue, asyncComputeCommandQueue))
        {
            selectedPageIndex = pageIndex;
            break;
        }
    }

    if (selectedPageIndex == InvalidPageIndex && m_Pages.size() < m_Desc.MaxFramePages)
    {
        DescriptorPage page;
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = m_Desc.ResourceDescriptorCapacity;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&page.ResourceDescriptorHeap)));
        const std::wstring pageName = L"Framework Bindless Frame Descriptor Heap " + std::to_wstring(m_Pages.size());
        ThrowIfFailed(page.ResourceDescriptorHeap->SetName(pageName.c_str()));
        m_Pages.push_back(std::move(page));
        selectedPageIndex = static_cast<uint32_t>(m_Pages.size() - 1u);
    }

    if (selectedPageIndex == InvalidPageIndex)
    {
        DescriptorPage& page = m_Pages.front();
        if (page.DirectFenceValue != 0u)
        {
            directCommandQueue.WaitForFenceValue(page.DirectFenceValue);
        }
        if (page.AsyncComputeFenceValue != 0u)
        {
            asyncComputeCommandQueue.WaitForFenceValue(page.AsyncComputeFenceValue);
        }
        selectedPageIndex = 0u;
    }

    m_CurrentPageIndex = selectedPageIndex;
    m_FrameActive = true;
    InitializePageLocked(GetCurrentPageLocked());
    for (uint32_t descriptorIndex = 0u; descriptorIndex < m_NextResourceDescriptorIndex; ++descriptorIndex)
    {
        if (m_ResourceDescriptorRevisions[descriptorIndex] != 0u)
        {
            EnsureResourceDescriptorOnCurrentPageLocked(descriptorIndex);
        }
    }
}

void BindlessDescriptorHeap::EndFrame(
    const uint64_t directFenceValue,
    const uint64_t asyncComputeFenceValue)
{
    std::lock_guard lock(m_Mutex);
    Assert(m_FrameActive, "Bindless descriptor heap frame scope is not active.");

    DescriptorPage& page = GetCurrentPageLocked();
    page.DirectFenceValue = directFenceValue;
    page.AsyncComputeFenceValue = asyncComputeFenceValue;
    m_FrameActive = false;
}

uint32_t BindlessDescriptorHeap::AddShaderResourceView(
    const Resource& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
{
    std::lock_guard lock(m_Mutex);
    Assert(!m_FrameActive, "Bindless resource descriptors must be created before BeginFrame.");
    if (srvDesc == nullptr)
    {
        const auto cachedDescriptor = m_DefaultShaderResourceDescriptors.find(&resource);
        if (cachedDescriptor != m_DefaultShaderResourceDescriptors.end())
        {
            CachedShaderResourceDescriptor& cached = cachedDescriptor->second;
            ID3D12Resource* nativeResource = resource.GetD3D12Resource().Get();
            if (cached.NativeResource != nativeResource)
            {
                UpdateShaderResourceViewLocked(cached.DescriptorIndex, resource, nullptr);
                cached.NativeResource = nativeResource;
            }
            return cached.DescriptorIndex;
        }
    }

    Assert(
        m_NextResourceDescriptorIndex < m_Desc.ResourceDescriptorCapacity,
        "Bindless resource descriptor heap capacity was exceeded.");

    const uint32_t descriptorIndex = m_NextResourceDescriptorIndex++;
    UpdateShaderResourceViewLocked(descriptorIndex, resource, srvDesc);
    if (srvDesc == nullptr)
    {
        m_DefaultShaderResourceDescriptors.emplace(
            &resource,
            CachedShaderResourceDescriptor{ descriptorIndex, resource.GetD3D12Resource().Get() });
    }
    return descriptorIndex;
}

void BindlessDescriptorHeap::UpdateShaderResourceView(
    const uint32_t descriptorIndex,
    const Resource& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
{
    std::lock_guard lock(m_Mutex);
    Assert(!m_FrameActive, "Bindless resource descriptors must be updated before BeginFrame.");
    UpdateShaderResourceViewLocked(descriptorIndex, resource, srvDesc);
}

void BindlessDescriptorHeap::UpdateShaderResourceViewLocked(
    const uint32_t descriptorIndex,
    const Resource& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");
    Assert(descriptorIndex < m_NextResourceDescriptorIndex, "Bindless resource descriptor index has not been allocated.");

    const D3D12_CPU_DESCRIPTOR_HANDLE destination = GetCanonicalResourceCpuHandle(descriptorIndex);
    if (dynamic_cast<const Texture*>(&resource) != nullptr)
    {
        m_Device->CreateShaderResourceView(resource.GetD3D12Resource().Get(), srvDesc, destination);
    }
    else
    {
        m_Device->CopyDescriptorsSimple(
            1u,
            destination,
            resource.GetShaderResourceView(srvDesc),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    ++m_ResourceDescriptorRevisions[descriptorIndex];
    if (m_ResourceDescriptorRevisions[descriptorIndex] == 0u)
    {
        ++m_ResourceDescriptorRevisions[descriptorIndex];
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetOrCreateDescriptorTable(
    const PipelineDescriptorTableAllocation& allocation)
{
    std::lock_guard lock(m_Mutex);
    Assert(allocation.IsValid(), "Bindless descriptor table allocation is invalid.");
    DescriptorPage& page = GetCurrentPageLocked();

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
        m_NextResourceDescriptorIndex += allocation.GetNumHandles();
        cacheIt = m_CachedDescriptorTables.emplace(cacheKey, cachedTable).first;
    }

    CachedDescriptorTable& cachedTable = cacheIt->second;
    Assert(cachedTable.DescriptorCount == allocation.GetNumHandles(), "Bindless descriptor table cache count mismatch.");
    const auto pageRevision = page.DescriptorTableRevisions.find(cacheKey);
    if (pageRevision == page.DescriptorTableRevisions.end() || pageRevision->second != allocation.GetRevision())
    {
        m_Device->CopyDescriptorsSimple(
            allocation.GetNumHandles(),
            GetPageResourceCpuHandle(page, cachedTable.DescriptorIndex),
            allocation.GetDescriptorHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        page.DescriptorTableRevisions[cacheKey] = allocation.GetRevision();
    }

    return GetPageResourceGpuHandle(page, cachedTable.DescriptorIndex);
}

ID3D12DescriptorHeap* BindlessDescriptorHeap::GetResourceDescriptorHeap()
{
    std::lock_guard lock(m_Mutex);
    return GetCurrentPageLocked().ResourceDescriptorHeap.Get();
}

uint32_t BindlessDescriptorHeap::GetResourceDescriptorCount() const
{
    std::lock_guard lock(m_Mutex);
    return m_NextResourceDescriptorIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetResourceGpuHandle(const uint32_t descriptorIndex)
{
    std::lock_guard lock(m_Mutex);
    EnsureResourceDescriptorOnCurrentPageLocked(descriptorIndex);
    return GetPageResourceGpuHandle(GetCurrentPageLocked(), descriptorIndex);
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetResourceCpuHandle(const uint32_t descriptorIndex) const
{
    std::lock_guard lock(m_Mutex);
    return GetCanonicalResourceCpuHandle(descriptorIndex);
}

BindlessDescriptorHeap::DescriptorPage& BindlessDescriptorHeap::GetCurrentPageLocked()
{
    Assert(m_FrameActive, "Bindless descriptor heap must begin a frame before descriptors are bound.");
    Assert(m_CurrentPageIndex < m_Pages.size(), "Bindless descriptor heap does not have an active frame page.");
    return m_Pages[m_CurrentPageIndex];
}

const BindlessDescriptorHeap::DescriptorPage& BindlessDescriptorHeap::GetCurrentPageLocked() const
{
    Assert(m_FrameActive, "Bindless descriptor heap must begin a frame before descriptors are bound.");
    Assert(m_CurrentPageIndex < m_Pages.size(), "Bindless descriptor heap does not have an active frame page.");
    return m_Pages[m_CurrentPageIndex];
}

void BindlessDescriptorHeap::InitializePageLocked(DescriptorPage& page)
{
    if (page.LayoutGeneration != m_LayoutGeneration)
    {
        page.ResourceRevisions.assign(m_Desc.ResourceDescriptorCapacity, 0u);
        page.DescriptorTableRevisions.clear();
        page.LayoutGeneration = m_LayoutGeneration;
    }
    page.DirectFenceValue = 0u;
    page.AsyncComputeFenceValue = 0u;
}

bool BindlessDescriptorHeap::IsPageAvailableLocked(
    const DescriptorPage& page,
    CommandQueue& directCommandQueue,
    CommandQueue& asyncComputeCommandQueue) const
{
    return
        (page.DirectFenceValue == 0u || directCommandQueue.IsFenceComplete(page.DirectFenceValue)) &&
        (page.AsyncComputeFenceValue == 0u || asyncComputeCommandQueue.IsFenceComplete(page.AsyncComputeFenceValue));
}

void BindlessDescriptorHeap::EnsureResourceDescriptorOnCurrentPageLocked(const uint32_t descriptorIndex)
{
    Assert(descriptorIndex < m_NextResourceDescriptorIndex, "Bindless resource descriptor index has not been allocated.");
    DescriptorPage& page = GetCurrentPageLocked();
    const uint64_t revision = m_ResourceDescriptorRevisions[descriptorIndex];
    Assert(revision != 0u, "Bindless resource descriptor has not been initialized.");
    if (page.ResourceRevisions[descriptorIndex] == revision)
    {
        return;
    }

    m_Device->CopyDescriptorsSimple(
        1u,
        GetPageResourceCpuHandle(page, descriptorIndex),
        GetCanonicalResourceCpuHandle(descriptorIndex),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    page.ResourceRevisions[descriptorIndex] = revision;
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetCanonicalResourceCpuHandle(const uint32_t descriptorIndex) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_CanonicalResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_ResourceDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetPageResourceCpuHandle(
    const DescriptorPage& page,
    const uint32_t descriptorIndex) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    D3D12_CPU_DESCRIPTOR_HANDLE handle = page.ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_ResourceDescriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessDescriptorHeap::GetPageResourceGpuHandle(
    const DescriptorPage& page,
    const uint32_t descriptorIndex) const
{
    Assert(descriptorIndex < m_Desc.ResourceDescriptorCapacity, "Bindless resource descriptor index is out of range.");

    D3D12_GPU_DESCRIPTOR_HANDLE handle = page.ResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_ResourceDescriptorSize;
    return handle;
}
//Modify End
