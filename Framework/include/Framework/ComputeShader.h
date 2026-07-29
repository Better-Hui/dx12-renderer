#pragma once

#include <memory.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-07-23 by BestHui
#include <DX12Library/ShaderUtils.h>
//Modify End

#include "ComputePipelineStateBuilder.h"
//Modify Begin:2026-07-24 by BestHui
#include "PipelineBindingSet.h"
#include "PipelineDescriptorPool.h"
#include "PipelineDescriptorSet.h"
#include "PipelineLayout.h"
#include "PipelineStateCache.h"
#include "PipelineStateKey.h"
//Modify End
#include "ShaderBlob.h"
#include "ShaderReflection.h"
//Modify Begin:2026-07-23 by BestHui
#include "ShaderResourceView.h"
#include "UnorderedAccessView.h"

#include <map>
#include <string>
#include <vector>
//Modify End

class CommandContext;

//Modify Begin:2026-07-23 by BestHui
struct ComputePipelineDesc
{
    struct BindingOverride
    {
        std::string Name;
        UINT DescriptorCount = 1;
    };

    std::vector<BindingOverride> BindingOverrides;
    UINT MaxDescriptorCount = 1024;
};

class ComputePipelineDescBuilder
{
public:
    static ComputePipelineDescBuilder ReflectedDefault(const ShaderBlob& shader);

    ComputePipelineDescBuilder& WithDescriptorArrayCount(std::string name, UINT descriptorCount);
    ComputePipelineDescBuilder& WithMaxDescriptorCount(UINT maxDescriptorCount);

    ComputePipelineDesc Build() const;

private:
    explicit ComputePipelineDescBuilder(ComputePipelineDesc desc);

    ComputePipelineDesc m_Desc;
};
//Modify End

class ComputeShader
{
public:
    ComputeShader(const ShaderBlob& shader, ComputePipelineDesc desc);

    bool HasConstantBuffer(const std::string& variableName) const;
    bool HasShaderResourceView(const std::string& variableName) const;
    bool HasUnorderedAccessView(const std::string& variableName) const;
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data) const
    {
        SetConstantBuffer(commandList, variableName, sizeof(T), &data);
    }

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const;

    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const;

    using ShaderMetadata = ShaderReflectionMetadata;

    const ShaderMetadata& GetShaderMetadata() const { return m_ShaderMetadata; }
//Modify Begin:2026-07-28 by BestHui
    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }
//Modify End


private:
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
