#include "DX12LibPCH.h"

#include "IndexBuffer.h"

#include <cassert>
//Modify Begin:2026-07-21 by BestHui
#include "D3D12DeviceContext.h"
#include "Helpers.h"
//Modify End

IndexBuffer::IndexBuffer(const std::wstring& name)
	: Buffer(name)
	, NumIndices(0)
	, IndexFormat(DXGI_FORMAT_UNKNOWN)
	, IndexBufferView({})
//Modify Begin:2026-07-21 by BestHui
	, m_Srv(m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
//Modify End
{
}

IndexBuffer::~IndexBuffer() = default;

void IndexBuffer::CreateViews(const size_t numElements, const size_t elementSize)
{
	assert(elementSize == 2 || elementSize == 4 && "Indices must be 16, or 32-bit integers.");

	NumIndices = numElements;
	IndexFormat = (elementSize == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	IndexBufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	IndexBufferView.SizeInBytes = static_cast<UINT>(numElements * elementSize);
	IndexBufferView.Format = IndexFormat;

//Modify Begin:2026-07-21 by BestHui
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = IndexFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = static_cast<UINT>(NumIndices);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	m_DeviceContext->GetDevice()->CreateShaderResourceView(m_d3d12Resource.Get(), &srvDesc, m_Srv.GetDescriptorHandle());
//Modify End
}

D3D12_CPU_DESCRIPTOR_HANDLE IndexBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
//Modify Begin:2026-07-21 by BestHui
//Modify Begin:2026-07-27 by BestHui
	if (srvDesc == nullptr)
	{
		return m_Srv.GetDescriptorHandle();
	}

	const size_t hash = std::hash<D3D12_SHADER_RESOURCE_VIEW_DESC>{}(*srvDesc);
	std::lock_guard<std::mutex> lock(m_CustomSrvsMutex);
	auto iter = m_CustomSrvs.find(hash);
	if (iter == m_CustomSrvs.end())
	{
		DescriptorAllocation srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_DeviceContext->GetDevice()->CreateShaderResourceView(m_d3d12Resource.Get(), srvDesc, srv.GetDescriptorHandle());
		iter = m_CustomSrvs.insert({ hash, std::move(srv) }).first;
	}

	return iter->second.GetDescriptorHandle();
//Modify End
//Modify End
}

D3D12_CPU_DESCRIPTOR_HANDLE IndexBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	throw std::exception("IndexBuffer::GetUnorderedAccessView should not be called.");
}
