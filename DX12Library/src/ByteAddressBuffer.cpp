#include "DX12LibPCH.h"
#include "ByteAddressBuffer.h"
#include "D3D12DeviceContext.h"

ByteAddressBuffer::ByteAddressBuffer(
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(name, std::move(deviceContext))
{
    m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

ByteAddressBuffer::ByteAddressBuffer(const D3D12_RESOURCE_DESC& resDesc,
    const size_t numElements, const size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(resDesc, numElements, elementSize, name, std::move(deviceContext))
{
    m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateViews(numElements, elementSize);
}

ByteAddressBuffer::ByteAddressBuffer(
    const D3D12_RESOURCE_DESC& resDesc,
    const ComPtr<ID3D12Heap>& pHeap,
    UINT64 heapOffset,
    size_t numElements,
    size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(resDesc, pHeap, heapOffset, numElements, elementSize, name, std::move(deviceContext))
{
    m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateViews(numElements, elementSize);
}

void ByteAddressBuffer::CreateViews(size_t numElements, size_t elementSize)
{
    const auto device = m_DeviceContext->GetDevice();

    // Make sure buffer size is aligned to 4 bytes.
    m_BufferSize = Math::AlignUp(numElements * elementSize, 4);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = static_cast<UINT>(m_BufferSize) / 4;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

    device->CreateShaderResourceView(m_d3d12Resource.Get(), &srvDesc, m_Srv.GetDescriptorHandle());

    if (GetD3D12ResourceDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.Buffer.NumElements = static_cast<UINT>(m_BufferSize) / 4;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

        device->CreateUnorderedAccessView(m_d3d12Resource.Get(), nullptr, &uavDesc, m_Uav.GetDescriptorHandle());
    }
}

//Modify Begin:2026-08-07 by BestHui
D3D12_CPU_DESCRIPTOR_HANDLE ByteAddressBuffer::GetUnorderedAccessView(
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    (void)uavDesc;
    Assert(SupportsUnorderedAccess(), "Byte-address buffer was not created with unordered-access usage.");
    return m_Uav.GetDescriptorHandle();
}
//Modify End
