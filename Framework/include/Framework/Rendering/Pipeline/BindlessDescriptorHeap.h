//Modify Begin:2026-07-30 by Hui
#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

class CommandList;
class CommandQueue;
class Resource;
struct PipelineDescriptorTableAllocation;

struct BindlessDescriptorHeapDesc
{
    uint32_t ResourceDescriptorCapacity = 65536;
    uint32_t MaxFramePages = 3;
};

class BindlessDescriptorHeap final
{
public:
    explicit BindlessDescriptorHeap(ID3D12Device2& device, BindlessDescriptorHeapDesc desc = {});

    void Reset();
    void BeginFrame(CommandQueue& directCommandQueue, CommandQueue& asyncComputeCommandQueue);
    void EndFrame(uint64_t directFenceValue, uint64_t asyncComputeFenceValue);
    uint32_t AddShaderResourceView(const Resource& resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);
    void UpdateShaderResourceView(uint32_t descriptorIndex, const Resource& resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);
    D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreateDescriptorTable(const PipelineDescriptorTableAllocation& allocation);

    ID3D12DescriptorHeap* GetResourceDescriptorHeap();
    uint32_t GetResourceDescriptorCapacity() const { return m_Desc.ResourceDescriptorCapacity; }
    uint32_t GetResourceDescriptorCount() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetResourceGpuHandle(uint32_t descriptorIndex = 0);
    D3D12_CPU_DESCRIPTOR_HANDLE GetResourceCpuHandle(uint32_t descriptorIndex = 0) const;

private:
//Modify Begin:2026-08-10 by Hui
    void UpdateShaderResourceViewLocked(
        uint32_t descriptorIndex,
        const Resource& resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc);
    struct CachedDescriptorTable
    {
        uint32_t DescriptorIndex = 0;
        uint32_t DescriptorCount = 0;
    };
    struct DescriptorPage
    {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ResourceDescriptorHeap;
        std::vector<uint64_t> ResourceRevisions;
        std::map<uint64_t, uint64_t> DescriptorTableRevisions;
        uint64_t DirectFenceValue = 0;
        uint64_t AsyncComputeFenceValue = 0;
        uint64_t LayoutGeneration = 0;
    };

    static constexpr uint32_t InvalidPageIndex = (std::numeric_limits<uint32_t>::max)();

    DescriptorPage& GetCurrentPageLocked();
    const DescriptorPage& GetCurrentPageLocked() const;
    void InitializePageLocked(DescriptorPage& page);
    bool IsPageAvailableLocked(
        const DescriptorPage& page,
        CommandQueue& directCommandQueue,
        CommandQueue& asyncComputeCommandQueue) const;
    void EnsureResourceDescriptorOnCurrentPageLocked(uint32_t descriptorIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCanonicalResourceCpuHandle(uint32_t descriptorIndex) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetPageResourceCpuHandle(const DescriptorPage& page, uint32_t descriptorIndex) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetPageResourceGpuHandle(const DescriptorPage& page, uint32_t descriptorIndex) const;
//Modify End
    BindlessDescriptorHeapDesc m_Desc;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CanonicalResourceDescriptorHeap;
    uint32_t m_ResourceDescriptorSize = 0;
    uint32_t m_NextResourceDescriptorIndex = 0;
//Modify Begin:2026-07-30 by Hui
    struct CachedShaderResourceDescriptor
    {
        uint32_t DescriptorIndex = 0;
        ID3D12Resource* NativeResource = nullptr;
    };
//Modify End
    std::map<const Resource*, CachedShaderResourceDescriptor> m_DefaultShaderResourceDescriptors;
    std::map<uint64_t, CachedDescriptorTable> m_CachedDescriptorTables;
    std::vector<uint64_t> m_ResourceDescriptorRevisions;
    std::vector<DescriptorPage> m_Pages;
    uint32_t m_CurrentPageIndex = InvalidPageIndex;
    uint64_t m_LayoutGeneration = 1;
    bool m_FrameActive = false;
    ID3D12Device2* m_Device = nullptr;
//Modify Begin:2026-08-10 by Hui
    mutable std::mutex m_Mutex;
//Modify End
};
//Modify End
