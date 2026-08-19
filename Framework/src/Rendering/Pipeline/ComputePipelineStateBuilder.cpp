//Modify Begin:2026-07-27 by Hui
#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>

#include <DX12Library/Helpers.h>

ComputePipelineStateBuilder::ComputePipelineStateBuilder() = default;

ComputePipelineStateBuilder::ComputePipelineStateBuilder(std::shared_ptr<RootSignature> rootSignature)
    : m_RootSignature(std::move(rootSignature))
{
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputePipelineStateBuilder::Build(Microsoft::WRL::ComPtr<ID3D12Device2> device) const
{
    Assert(m_RootSignature != nullptr, "Root signature cannot be null.");
    Assert(m_ComputeShader != nullptr, "Compute Shader cannot be null.");

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS Cs;
    } pipelineStateStream;

    pipelineStateStream.RootSignature = m_RootSignature->GetRootSignature().Get();
    pipelineStateStream.Cs = CD3DX12_SHADER_BYTECODE(m_ComputeShader->GetBufferPointer(), m_ComputeShader->GetBufferSize());

    const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc{ sizeof(PipelineStateStream), &pipelineStateStream };

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)));
    return pipelineState;
}

ComputePipelineStateKey ComputePipelineStateBuilder::CreateKey(
    const size_t definesHash,
    const uint32_t shaderModelMajor,
    const uint32_t shaderModelMinor) const
{
    ComputePipelineStateKey key;
    key.Shader = MakePipelineShaderBytecodeKey(m_ComputeShader);
    key.LayoutHash = MakePipelineRootSignatureHash(m_RootSignature.get());
    key.DefinesHash = definesHash;
    key.ShaderModelMajor = shaderModelMajor;
    key.ShaderModelMinor = shaderModelMinor;
    return key;
}

ComputePipelineStateBuilder& ComputePipelineStateBuilder::WithRootSignature(std::shared_ptr<RootSignature> rootSignature)
{
    Assert(rootSignature != nullptr, "Root signature cannot be null.");

    m_RootSignature = std::move(rootSignature);
    return *this;
}

ComputePipelineStateBuilder& ComputePipelineStateBuilder::WithShader(const Microsoft::WRL::ComPtr<ID3DBlob>& computeShader)
{
    Assert(computeShader != nullptr, "Compute Shader cannot be null.");

    m_ComputeShader = computeShader;
    return *this;
}
//Modify End
