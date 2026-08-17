#pragma once
//Modify Begin:2026-07-23 by Hui

#include <DX12Library/RootSignature.h>
//Modify Begin:2026-07-27 by Hui
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
//Modify End

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include <memory>

class ComputePipelineStateBuilder final
{
public:
    ComputePipelineStateBuilder();
    explicit ComputePipelineStateBuilder(std::shared_ptr<RootSignature> rootSignature);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> Build(Microsoft::WRL::ComPtr<ID3D12Device2> device) const;
//Modify Begin:2026-07-27 by Hui
    ComputePipelineStateKey CreateKey(
        size_t definesHash = 0,
        uint32_t shaderModelMajor = 0,
        uint32_t shaderModelMinor = 0) const;
//Modify End

    ComputePipelineStateBuilder& WithRootSignature(std::shared_ptr<RootSignature> rootSignature);
    ComputePipelineStateBuilder& WithShader(const Microsoft::WRL::ComPtr<ID3DBlob>& computeShader);

private:
    std::shared_ptr<RootSignature> m_RootSignature;
    Microsoft::WRL::ComPtr<ID3DBlob> m_ComputeShader;
};
//Modify End
