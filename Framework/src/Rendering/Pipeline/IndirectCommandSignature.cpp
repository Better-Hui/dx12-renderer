//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>

#include <DX12Library/Helpers.h>
#include <DX12Library/RootSignature.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <vector>

namespace
{
    D3D12_INDIRECT_ARGUMENT_TYPE ToD3D12ArgumentType(const IndirectArgumentType type)
    {
        switch (type)
        {
        case IndirectArgumentType::Draw:
            return D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        case IndirectArgumentType::DrawIndexed:
            return D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        case IndirectArgumentType::Dispatch:
            return D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        case IndirectArgumentType::DispatchRays:
            return D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;
        case IndirectArgumentType::DispatchMesh:
            return D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        case IndirectArgumentType::RootConstants:
        default:
            Assert(false, "Root constants require a dedicated D3D12 indirect argument descriptor.");
            return D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        }
    }

    IndirectCommandPipeline GetPipelineForArgument(const IndirectArgumentType type)
    {
        switch (type)
        {
        case IndirectArgumentType::Draw:
        case IndirectArgumentType::DrawIndexed:
        case IndirectArgumentType::DispatchMesh:
            return IndirectCommandPipeline::Graphics;
        case IndirectArgumentType::Dispatch:
            return IndirectCommandPipeline::Compute;
        case IndirectArgumentType::DispatchRays:
            return IndirectCommandPipeline::RayTracing;
        case IndirectArgumentType::RootConstants:
        default:
            Assert(false, "Root constants do not define an indirect command pipeline.");
            return IndirectCommandPipeline::Graphics;
        }
    }
}

IndirectCommandSignature::IndirectCommandSignature(
    FrameworkDeviceContext& deviceContext,
    const IndirectCommandSignatureDesc& desc)
{
    Assert(!desc.Arguments.empty(), "Indirect command signatures require at least one argument.");
    Assert(desc.ByteStride > 0u, "Indirect command signature byte stride must be positive.");

    std::vector<D3D12_INDIRECT_ARGUMENT_DESC> d3d12Arguments;
    d3d12Arguments.reserve(desc.Arguments.size());

    uint32_t executionArgumentCount = 0u;
    bool containsRootConstants = false;
    for (const IndirectArgumentDesc& argument : desc.Arguments)
    {
        D3D12_INDIRECT_ARGUMENT_DESC d3d12Argument = {};
        if (argument.Type == IndirectArgumentType::RootConstants)
        {
            Assert(argument.RootConstantCount > 0u, "Indirect root-constant arguments must contain at least one value.");
            d3d12Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            d3d12Argument.Constant.RootParameterIndex = argument.RootParameterIndex;
            d3d12Argument.Constant.DestOffsetIn32BitValues = argument.RootConstantOffset;
            d3d12Argument.Constant.Num32BitValuesToSet = argument.RootConstantCount;
            containsRootConstants = true;
        }
        else
        {
            ++executionArgumentCount;
            m_ExecutionArgumentType = argument.Type;
            m_D3D12ExecutionArgumentType = ToD3D12ArgumentType(argument.Type);
            d3d12Argument.Type = m_D3D12ExecutionArgumentType;
        }
        d3d12Arguments.push_back(d3d12Argument);
    }

    Assert(executionArgumentCount == 1u, "Indirect command signatures must contain exactly one execution argument.");
    Assert(
        !containsRootConstants || desc.RootSignatureRef != nullptr,
        "Indirect root constants require a root signature.");
    Assert(
        m_ExecutionArgumentType != IndirectArgumentType::DispatchRays ||
            desc.Arguments.size() == 1u,
        "DispatchRays indirect signatures cannot contain root constants or additional arguments.");

    m_Pipeline = GetPipelineForArgument(m_ExecutionArgumentType);

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
    commandSignatureDesc.ByteStride = desc.ByteStride;
    commandSignatureDesc.NumArgumentDescs = static_cast<UINT>(d3d12Arguments.size());
    commandSignatureDesc.pArgumentDescs = d3d12Arguments.data();

    const auto& device = deviceContext.GetDevice();
    ThrowIfFailed(device->CreateCommandSignature(
        &commandSignatureDesc,
        desc.RootSignatureRef != nullptr ? desc.RootSignatureRef->GetRootSignature().Get() : nullptr,
        IID_PPV_ARGS(&m_CommandSignature)));
    ThrowIfFailed(m_CommandSignature->SetName(L"Indirect Command Signature"));
}
//Modify End
