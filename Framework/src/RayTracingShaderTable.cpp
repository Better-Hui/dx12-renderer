#include <Framework/RayTracingShaderTable.h>

#include <DX12Library/Helpers.h>

#include <d3dx12.h>

#include <algorithm>
#include <cstring>

//Modify Begin:2026-07-24 by BestHui

void RayTracingShaderTable::Reset(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    const std::vector<RayTracingShaderRecord>& shaderRecords,
    const uint32_t shaderIdentifierSize,
    const wchar_t* name)
{
    uint32_t maxRecordSize = shaderIdentifierSize;
    for (const RayTracingShaderRecord& record : shaderRecords)
    {
        maxRecordSize = std::max<uint32_t>(
            maxRecordSize,
            shaderIdentifierSize + static_cast<uint32_t>(record.LocalRootArguments.size()));
    }

    m_StrideInBytes = Math::AlignUp(maxRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    m_SizeInBytes = static_cast<uint64_t>(m_StrideInBytes) * shaderRecords.size();

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_SizeInBytes);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_Resource)));

    if (name != nullptr)
    {
        m_Resource->SetName(name);
    }

    uint8_t* mappedData = nullptr;
    ThrowIfFailed(m_Resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

    for (size_t i = 0; i < shaderRecords.size(); ++i)
    {
        const RayTracingShaderRecord& record = shaderRecords[i];
        uint8_t* recordData = mappedData + i * m_StrideInBytes;
        std::memcpy(recordData, record.ShaderIdentifier, shaderIdentifierSize);
        if (!record.LocalRootArguments.empty())
        {
            std::memcpy(recordData + shaderIdentifierSize, record.LocalRootArguments.data(), record.LocalRootArguments.size());
        }
    }

    m_Resource->Unmap(0, nullptr);
}

D3D12_GPU_VIRTUAL_ADDRESS RayTracingShaderTable::GetGpuVirtualAddress() const
{
    return m_Resource->GetGPUVirtualAddress();
}

uint64_t RayTracingShaderTable::GetSizeInBytes() const
{
    return m_SizeInBytes;
}

uint32_t RayTracingShaderTable::GetStrideInBytes() const
{
    return m_StrideInBytes;
}

//Modify End
