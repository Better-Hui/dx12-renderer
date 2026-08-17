#include "VertexBuffer.h"

#include "DX12LibPCH.h"
//Modify Begin:2026-07-21 by Hui
#include "D3D12DeviceContext.h"
#include "Helpers.h"
//Modify End


VertexBuffer::VertexBuffer(const std::wstring& name)
	: Buffer(name)
	, NumVertices(0)
	, VertexStride(0)
	, VertexBufferView({})
{
}

VertexBuffer::~VertexBuffer()
{
}

void VertexBuffer::CreateViews(const size_t numElements, const size_t elementSize)
{
	NumVertices = numElements;
	VertexStride = elementSize;

	VertexBufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	VertexBufferView.SizeInBytes = static_cast<UINT>(NumVertices * VertexStride);
	VertexBufferView.StrideInBytes = static_cast<UINT>(VertexStride);

//Modify Begin:2026-07-21 by Hui
//Modify Begin:2026-08-12 by Hui
	if (m_Srv.IsNull())
	{
		m_Srv = m_DeviceContext->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
//Modify End
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = static_cast<UINT>(NumVertices);
	srvDesc.Buffer.StructureByteStride = static_cast<UINT>(VertexStride);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	m_DeviceContext->GetDevice()->CreateShaderResourceView(m_d3d12Resource.Get(), &srvDesc, m_Srv.GetDescriptorHandle());
//Modify End
}

D3D12_CPU_DESCRIPTOR_HANDLE VertexBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
//Modify Begin:2026-07-21 by Hui
//Modify Begin:2026-07-27 by Hui
//Modify Begin:2026-08-12 by Hui
	Assert(IsValid(), "Vertex-buffer SRV is requested before the buffer is initialized.");
	Assert(m_DeviceContext != nullptr, "Vertex buffer has no D3D12 device context.");
//Modify End
	if (srvDesc == nullptr)
	{
//Modify Begin:2026-08-12 by Hui
		Assert(!m_Srv.IsNull(), "Vertex-buffer SRV is requested before the buffer is initialized.");
//Modify End
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

D3D12_CPU_DESCRIPTOR_HANDLE VertexBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	throw std::exception("VertexBuffer::GetUnorderedAccessView should not be called.");
}
