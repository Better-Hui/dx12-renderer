//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <DX12Library/DescriptorAllocation.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <functional>
#include <memory>

class CommandQueue;

struct FrameworkDeviceContextDesc
{
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> ComputeQueue;
    std::shared_ptr<CommandQueue> CopyQueue;
    std::function<DescriptorAllocation(D3D12_DESCRIPTOR_HEAP_TYPE, uint32_t)> AllocateDescriptors;
};

class FrameworkDeviceContext final
{
public:
    explicit FrameworkDeviceContext(FrameworkDeviceContextDesc desc);

    const Microsoft::WRL::ComPtr<ID3D12Device2>& GetDevice() const { return m_Desc.Device; }
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
    DescriptorAllocation AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t descriptorCount = 1) const;
    void Flush() const;

private:
    FrameworkDeviceContextDesc m_Desc;
};
//Modify End
