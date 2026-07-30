//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/IndirectDrawCommandSignature.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>

IndirectDrawCommandSignature::IndirectDrawCommandSignature()
{
    D3D12_INDIRECT_ARGUMENT_DESC argumentDesc = {};
    argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &argumentDesc;

    const auto device = Application::Get().GetDevice();
    ThrowIfFailed(device->CreateCommandSignature(
        &commandSignatureDesc,
        nullptr,
        IID_PPV_ARGS(&m_CommandSignature)));
    ThrowIfFailed(m_CommandSignature->SetName(L"Indirect Draw Command Signature"));
}
//Modify End
