#include "DX12LibPCH.h"
#include "Buffer.h"

#include "D3D12DeviceContext.h"

Buffer::Buffer(const std::wstring& name, std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Resource(name, std::move(deviceContext))
{}

Buffer::Buffer(const D3D12_RESOURCE_DESC& resDesc,
    const size_t numElements, const size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Resource(resDesc, nullptr, name, std::move(deviceContext))
{}

Buffer::Buffer(
    const D3D12_RESOURCE_DESC& resDesc,
    const Microsoft::WRL::ComPtr<ID3D12Heap>& pHeap,
    UINT64 heapOffset,
    size_t numElements,
    size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Resource(resDesc, pHeap, heapOffset, nullptr, name, std::move(deviceContext))
{

}
