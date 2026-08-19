//Modify Begin:2026-08-19 by Hui
#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <span>

class FrameworkDeviceContext;
class RootSignature;

enum class IndirectArgumentType : uint8_t
{
    RootConstants,
    Draw,
    DrawIndexed,
    Dispatch,
    DispatchRays,
    DispatchMesh,
};

enum class IndirectCommandPipeline : uint8_t
{
    Graphics,
    Compute,
    RayTracing,
};

struct IndirectArgumentDesc
{
    IndirectArgumentType Type = IndirectArgumentType::Draw;
    uint32_t RootParameterIndex = 0;
    uint32_t RootConstantOffset = 0;
    uint32_t RootConstantCount = 0;
};

struct IndirectCommandSignatureDesc
{
    std::span<const IndirectArgumentDesc> Arguments;
    uint32_t ByteStride = 0;
    const RootSignature* RootSignatureRef = nullptr;
};

class IndirectCommandSignature final
{
public:
    IndirectCommandSignature(
        FrameworkDeviceContext& deviceContext,
        const IndirectCommandSignatureDesc& desc);

    [[nodiscard]] const Microsoft::WRL::ComPtr<ID3D12CommandSignature>& GetD3D12CommandSignature() const
    {
        return m_CommandSignature;
    }

    [[nodiscard]] IndirectArgumentType GetExecutionArgumentType() const
    {
        return m_ExecutionArgumentType;
    }

    [[nodiscard]] IndirectCommandPipeline GetPipeline() const
    {
        return m_Pipeline;
    }

    [[nodiscard]] D3D12_INDIRECT_ARGUMENT_TYPE GetD3D12ExecutionArgumentType() const
    {
        return m_D3D12ExecutionArgumentType;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_CommandSignature;
    IndirectArgumentType m_ExecutionArgumentType = IndirectArgumentType::Draw;
    IndirectCommandPipeline m_Pipeline = IndirectCommandPipeline::Graphics;
    D3D12_INDIRECT_ARGUMENT_TYPE m_D3D12ExecutionArgumentType = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
};
//Modify End
