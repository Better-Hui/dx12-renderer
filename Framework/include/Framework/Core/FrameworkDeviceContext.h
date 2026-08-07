//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <DX12Library/DescriptorAllocation.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <functional>
#include <memory>

class CommandQueue;
class StreamlineRuntime;

struct FrameworkDeviceContextDesc
{
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> ComputeQueue;
    std::shared_ptr<CommandQueue> CopyQueue;
    std::shared_ptr<StreamlineRuntime> Streamline;
    std::function<DescriptorAllocation(D3D12_DESCRIPTOR_HEAP_TYPE, uint32_t)> AllocateDescriptors;
    std::function<bool(bool)> SetFrameGenerationEnabled;
};

class FrameworkDeviceContext final
{
public:
    explicit FrameworkDeviceContext(FrameworkDeviceContextDesc desc);

    const Microsoft::WRL::ComPtr<ID3D12Device2>& GetDevice() const { return m_Desc.Device; }
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
    std::shared_ptr<StreamlineRuntime> GetStreamlineRuntime() const { return m_Desc.Streamline; }
    DescriptorAllocation AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t descriptorCount = 1) const;
    void Flush() const;
    bool SetFrameGenerationEnabled(bool enabled) const;

private:
    FrameworkDeviceContextDesc m_Desc;
};
//Modify End
