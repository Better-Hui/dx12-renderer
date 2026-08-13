#pragma once

#include <memory.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-07-23 by BestHui
#include <DX12Library/ShaderUtils.h>
//Modify End

#include <Framework/Rendering/Pipeline/ComputePipelineStateBuilder.h>
//Modify Begin:2026-07-24 by BestHui
#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
//Modify End
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
//Modify Begin:2026-07-23 by BestHui
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <map>
#include <span>
#include <string>
#include <vector>
//Modify End

class CommandContext;
class StructuredBuffer;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

//Modify Begin:2026-07-23 by BestHui
struct ComputePipelineDesc
{
    struct BindingOverride
    {
        std::string Name;
        UINT DescriptorCount = 1;
    };

    std::vector<BindingOverride> BindingOverrides;
//Modify Begin:2026-08-12 by BestHui
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
//Modify End
    UINT MaxDescriptorCount = 1024;
//Modify Begin:2026-07-30 by BestHui
    D3D12_ROOT_SIGNATURE_FLAGS RootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
//Modify End
};

class ComputePipelineDescBuilder
{
public:
    static ComputePipelineDescBuilder ReflectedDefault(const ShaderBlob& shader);

    ComputePipelineDescBuilder& WithDescriptorArrayCount(std::string name, UINT descriptorCount);
//Modify Begin:2026-08-12 by BestHui
    ComputePipelineDescBuilder& WithStaticSamplerContract(PipelineStaticSamplerContract contract);
    ComputePipelineDescBuilder& WithCommonRootSignatureStaticSamplers();
//Modify End
    ComputePipelineDescBuilder& WithMaxDescriptorCount(UINT maxDescriptorCount);
//Modify Begin:2026-07-30 by BestHui
    ComputePipelineDescBuilder& WithRootSignatureFlags(D3D12_ROOT_SIGNATURE_FLAGS flags);
    ComputePipelineDescBuilder& WithDirectlyIndexedResourceHeap();
//Modify End

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
//Modify Begin:2026-07-30 by BestHui
    bool HasAccelerationStructure(const std::string& variableName) const;
//Modify End
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data) const
    {
        SetConstantBuffer(commandList, variableName, sizeof(T), &data);
    }

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews) const;
//Modify Begin:2026-07-30 by BestHui
    void SetStructuredBuffer(CommandList& commandList, const std::string& variableName, const StructuredBuffer& buffer) const;
//Modify End
    void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const;

    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(CommandList& commandList, const std::string& variableName, const RayTracingAccelerationStructure& accelerationStructure) const;

    using ShaderMetadata = ShaderReflectionMetadata;

    const ShaderMetadata& GetShaderMetadata() const { return m_ShaderMetadata; }
//Modify Begin:2026-07-28 by BestHui
    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }
//Modify End


private:
//Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext& GetDeviceContext() const { return m_DeviceContext; }
//Modify End
    //Modify Begin:2026-07-23 by BestHui
//Modify Begin:2026-07-29 by BestHui
    friend class CommandContext;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const;
    const RootSignature& GetRootSignature() const { return *m_RootSignature; }
    const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
    const PipelineDescriptorPool& GetDescriptorPool() const { return m_DescriptorPool; }
    bool UsesReflectedRootSignature() const { return true; }
    void StageDefaultDescriptorTables(CommandList& commandList) const;
//Modify End
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
//Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorPool m_DescriptorPool;
//Modify End
//Modify Begin:2026-07-27 by BestHui
    mutable PipelineStateCache<ComputePipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateCache;
//Modify End
    //Modify End
};
