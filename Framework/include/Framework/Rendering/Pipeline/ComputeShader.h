#pragma once

#include <memory.h>

#include <d3d12.h>
#include <d3dx12/d3dx12.h>
#include <wrl.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-19 by Hui
#include <DX12Library/ShaderUtils.h>
//Modify End

#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
//Modify End
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <map>
#include <span>
#include <string>
#include <vector>
//Modify End

class CommandContext;
class StructuredBuffer;
//Modify Begin:2026-08-19 by Hui
class FrameworkDeviceContext;
//Modify End

//Modify Begin:2026-08-19 by Hui
struct ComputePipelineDesc
{
    struct BindingOverride
    {
        std::string Name;
        UINT DescriptorCount = 1;
    };

    std::vector<BindingOverride> BindingOverrides;
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
    UINT MaxDescriptorCount = 1024;
    D3D12_ROOT_SIGNATURE_FLAGS RootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
};

class ComputePipelineDescBuilder
{
public:
    static ComputePipelineDescBuilder ReflectedDefault(const ShaderBlob& shader);

    ComputePipelineDescBuilder& WithDescriptorArrayCount(std::string name, UINT descriptorCount);
    ComputePipelineDescBuilder& WithStaticSamplerContract(PipelineStaticSamplerContract contract);
    ComputePipelineDescBuilder& WithCommonRootSignatureStaticSamplers();
    ComputePipelineDescBuilder& WithMaxDescriptorCount(UINT maxDescriptorCount);
    ComputePipelineDescBuilder& WithRootSignatureFlags(D3D12_ROOT_SIGNATURE_FLAGS flags);
    ComputePipelineDescBuilder& WithDirectlyIndexedResourceHeap();

    ComputePipelineDesc Build() const;

private:
    explicit ComputePipelineDescBuilder(ComputePipelineDesc desc);

    ComputePipelineDesc m_Desc;
};
//Modify End

class ComputeShader
{
public:
    ComputeShader(FrameworkDeviceContext& deviceContext, const ShaderBlob& shader, ComputePipelineDesc desc);

    bool HasConstantBuffer(const std::string& variableName) const;
    bool HasShaderResourceView(const std::string& variableName) const;
    bool HasUnorderedAccessView(const std::string& variableName) const;
    bool HasAccelerationStructure(const std::string& variableName) const;
    using ShaderMetadata = ShaderReflectionMetadata;

    const ShaderMetadata& GetShaderMetadata() const { return m_ShaderMetadata; }
//Modify Begin:2026-08-19 by Hui
    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }
//Modify End


private:
//Modify Begin:2026-08-19 by Hui
    FrameworkDeviceContext& GetDeviceContext() const { return m_DeviceContext; }
//Modify End
//Modify Begin:2026-08-19 by Hui
    friend class CommandContext;
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data) const
    {
        SetConstantBuffer(commandList, variableName, sizeof(T), &data);
    }

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource) const;
    void SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews) const;
    void SetStructuredBuffer(CommandList& commandList, const std::string& variableName, const StructuredBuffer& buffer) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const;
    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(CommandList& commandList, const std::string& variableName, const RayTracingAccelerationStructure& accelerationStructure) const;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const;
    const RootSignature& GetRootSignature() const { return *m_RootSignature; }
    const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
    const PipelineDescriptorPool& GetDescriptorPool() const { return m_DescriptorPool; }
    bool UsesReflectedRootSignature() const { return true; }
    void StageDefaultDescriptorTables(CommandList& commandList) const;
    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);
    void BuildReflectedRootSignature(const ComputePipelineDesc& desc);
    const DescriptorBindingInfo& GetReflectedBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const;

    FrameworkDeviceContext& m_DeviceContext;
    std::shared_ptr<RootSignature> m_RootSignature;
    ComputePipelineStateBuilder m_PipelineStateBuilder;
    Microsoft::WRL::ComPtr<ID3DBlob> m_Shader;
    ShaderMetadata m_ShaderMetadata;
    std::unique_ptr<PipelineLayout> m_PipelineLayout;
    std::unique_ptr<PipelineBindingSet> m_BindingSet;
    std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
    PipelineDescriptorPool m_DescriptorPool;
    mutable PipelineStateCache<ComputePipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateCache;
    //Modify End
};
//Modify End
