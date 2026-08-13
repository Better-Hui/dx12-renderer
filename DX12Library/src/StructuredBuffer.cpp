#include "StructuredBuffer.h"

#include <d3dx12.h>

#include "Buffer.h"
#include "D3D12DeviceContext.h"
#include "Helpers.h"

namespace
{
    UINT64 GetBufferOffset(
        const D3D12_RESOURCE_DESC& resourceDesc,
        UINT64 baseOffset,
        const D3D12DeviceContext& deviceContext)
    {
        const auto pDevice = deviceContext.GetDevice();
        D3D12_RESOURCE_DESC descs[2] = {
            StructuredBuffer::COUNTER_DESC,
            resourceDesc,
        };
        const auto allocationInfo = pDevice->GetResourceAllocationInfo(0, 2, descs);
        return Math::AlignUp(baseOffset + StructuredBuffer::COUNTER_DESC.Width, allocationInfo.Alignment);
    }
}

StructuredBuffer::StructuredBuffer(
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(name, std::move(deviceContext))
    , m_NumElements(0)
    , m_ElementSize(0)
{
}

StructuredBuffer::StructuredBuffer(
    const D3D12_RESOURCE_DESC& resourceDesc,
    const size_t numElements,
    const size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(resourceDesc, numElements, elementSize, name, std::move(deviceContext))
    , m_NumElements(numElements)
    , m_ElementSize(elementSize)
    , m_CounterBuffer(std::make_shared<ByteAddressBuffer>(
        COUNTER_DESC,
        1,
        4,
        name + L" Counter",
        m_DeviceContext))
{
    m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateViews(m_NumElements, m_ElementSize);
}

StructuredBuffer::StructuredBuffer(
    const D3D12_RESOURCE_DESC& resourceDesc,
    const Microsoft::WRL::ComPtr<ID3D12Heap>& pHeap,
    UINT64 heapOffset,
    size_t numElements,
    size_t elementSize,
    const std::wstring& name,
    std::shared_ptr<D3D12DeviceContext> deviceContext)
    : Buffer(
        resourceDesc,
        pHeap,
        GetBufferOffset(resourceDesc, heapOffset, *deviceContext),
        numElements,
        elementSize,
        name,
        std::move(deviceContext))
    , m_NumElements(numElements)
    , m_ElementSize(elementSize)
    , m_CounterBuffer(std::make_shared<ByteAddressBuffer>(
        COUNTER_DESC,
        pHeap,
        heapOffset,
        1,
        4,
        name + L" Counter",
        m_DeviceContext))
{
    m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CreateViews(m_NumElements, m_ElementSize);
}

size_t StructuredBuffer::GetNumElements() const
{
    return m_NumElements;
}

size_t StructuredBuffer::GetElementSize() const
{
    return m_ElementSize;
}

void StructuredBuffer::CreateViews(size_t numElements, size_t elementSize)
{
    const auto device = m_DeviceContext->GetDevice();

    m_NumElements = numElements;
    m_ElementSize = elementSize;
//Modify Begin:2026-08-12 by BestHui
    if (m_Srv.IsNull())
    {
        m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (m_Uav.IsNull())
    {
        m_Uav = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (m_CounterBuffer == nullptr)
    {
        m_CounterBuffer = std::make_shared<ByteAddressBuffer>(
            COUNTER_DESC,
            1,
            4,
            GetName() + L" Counter",
            m_DeviceContext);
    }
//Modify End

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = static_cast<UINT>(m_NumElements);
    srvDesc.Buffer.StructureByteStride = static_cast<UINT>(m_ElementSize);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(m_d3d12Resource.Get(),
        &srvDesc,
        m_Srv.GetDescriptorHandle());

    if ((GetD3D12ResourceDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.NumElements = static_cast<UINT>(m_NumElements);
        uavDesc.Buffer.StructureByteStride = static_cast<UINT>(m_ElementSize);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        device->CreateUnorderedAccessView(m_d3d12Resource.Get(),
            m_CounterBuffer->GetD3D12Resource().Get(),
            &uavDesc,
            m_Uav.GetDescriptorHandle());
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE StructuredBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc /*= nullptr*/) const
{
    return m_Srv.GetDescriptorHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE StructuredBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc /*= nullptr*/) const
{
//Modify Begin:2026-08-07 by BestHui
    Assert(SupportsUnorderedAccess(), "Structured buffer was not created with unordered-access usage.");
    if (uavDesc != nullptr)
    {
        Assert(uavDesc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER, "Structured-buffer UAV must use a buffer view.");
        Assert(uavDesc->Format == DXGI_FORMAT_UNKNOWN, "Structured-buffer UAV must use DXGI_FORMAT_UNKNOWN.");
        Assert(
            uavDesc->Buffer.StructureByteStride == m_ElementSize,
            "Structured-buffer UAV stride does not match the buffer element size.");
    }
//Modify End
    return m_Uav.GetDescriptorHandle();
}

ByteAddressBuffer& StructuredBuffer::GetCounterBuffer()
{
//Modify Begin:2026-08-12 by BestHui
    Assert(m_CounterBuffer != nullptr, "Structured-buffer counter is unavailable before buffer initialization.");
//Modify End
    return *m_CounterBuffer;
}

const std::shared_ptr<ByteAddressBuffer>& StructuredBuffer::GetCounterBufferPtr() const
{
//Modify Begin:2026-08-12 by BestHui
    Assert(m_CounterBuffer != nullptr, "Structured-buffer counter is unavailable before buffer initialization.");
//Modify End
    return m_CounterBuffer;
}
