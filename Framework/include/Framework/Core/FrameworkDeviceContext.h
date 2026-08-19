//Modify Begin:2026-08-07 by Hui
#pragma once

#include <DX12Library/DescriptorAllocation.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <Framework/Rendering/Upscaling/FrameFeaturesRuntime.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <memory>

class CommandQueue;
class D3D12DeviceContext;
struct FrameworkDeviceContextDesc
{
    std::shared_ptr<D3D12DeviceContext> DeviceContext;
    std::shared_ptr<CommandQueue> DirectQueue;
    std::shared_ptr<CommandQueue> ComputeQueue;
    std::shared_ptr<CommandQueue> CopyQueue;
    std::shared_ptr<FrameFeaturesRuntime> FrameFeatures;
    std::shared_ptr<FrameGenerationController> FrameGeneration;
};

class FrameworkDeviceContext final
{
public:
    explicit FrameworkDeviceContext(FrameworkDeviceContextDesc desc);

    const Microsoft::WRL::ComPtr<ID3D12Device2>& GetDevice() const { return m_Desc.DeviceContext->GetDevice(); }
    const std::shared_ptr<D3D12DeviceContext>& GetD3D12DeviceContext() const { return m_Desc.DeviceContext; }
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
    std::shared_ptr<FrameFeaturesRuntime> GetFrameFeaturesRuntime() const { return m_Desc.FrameFeatures; }
    FrameGenerationController* GetFrameGenerationController() const { return m_Desc.FrameGeneration.get(); }
    DescriptorAllocation AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t descriptorCount = 1) const
    {
        return m_Desc.DeviceContext->AllocateDescriptors(type, descriptorCount);
    }
    void Flush() const;

private:
    FrameworkDeviceContextDesc m_Desc;
};
//Modify End
