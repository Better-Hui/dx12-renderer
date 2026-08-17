#pragma once

//Modify Begin:2026-07-24 by Hui

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <vector>

struct RayTracingShaderRecord
{
    const void* ShaderIdentifier = nullptr;
    std::vector<uint8_t> LocalRootArguments;
};

class RayTracingShaderTable
{
public:
    void Reset(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const std::vector<RayTracingShaderRecord>& shaderRecords,
        uint32_t shaderIdentifierSize,
        const wchar_t* name);

    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
    uint64_t GetSizeInBytes() const;
    uint32_t GetStrideInBytes() const;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
    uint64_t m_SizeInBytes = 0;
    uint32_t m_StrideInBytes = 0;
};

//Modify End
