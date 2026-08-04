//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/IndirectDrawCommandSignature.h>

#include <DX12Library/Helpers.h>
#include <DX12Library/RootSignature.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End

IndirectDrawCommandSignature::IndirectDrawCommandSignature(FrameworkDeviceContext& deviceContext)
{
    D3D12_INDIRECT_ARGUMENT_DESC argumentDesc = {};
    argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &argumentDesc;

    const auto& device = deviceContext.GetDevice();
    ThrowIfFailed(device->CreateCommandSignature(
        &commandSignatureDesc,
        nullptr,
        IID_PPV_ARGS(&m_CommandSignature)));
    ThrowIfFailed(m_CommandSignature->SetName(L"Indirect Draw Command Signature"));
}

//Modify Begin:2026-07-31 by BestHui
IndirectDrawCommandSignature::IndirectDrawCommandSignature(
    FrameworkDeviceContext& deviceContext,
    const RootSignature& rootSignature,
    const UINT rootParameterIndex,
    const UINT rootConstantCount,
    const UINT byteStride)
{
    D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[2] = {};
    argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argumentDescs[0].Constant.RootParameterIndex = rootParameterIndex;
    argumentDescs[0].Constant.DestOffsetIn32BitValues = 0u;
    argumentDescs[0].Constant.Num32BitValuesToSet = rootConstantCount;
    argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.ByteStride = byteStride;
    commandSignatureDesc.NumArgumentDescs = _countof(argumentDescs);
    commandSignatureDesc.pArgumentDescs = argumentDescs;

    const auto& device = deviceContext.GetDevice();
    ThrowIfFailed(device->CreateCommandSignature(
        &commandSignatureDesc,
        rootSignature.GetRootSignature().Get(),
        IID_PPV_ARGS(&m_CommandSignature)));
    ThrowIfFailed(m_CommandSignature->SetName(L"Indirect Draw Root Constant Command Signature"));
}
//Modify End
//Modify End
