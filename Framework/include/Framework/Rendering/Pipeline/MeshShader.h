#pragma once

//Modify Begin:2026-07-30 by BestHui

#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <functional>
#include <memory>
#include <span>
#include <string>

class CommandContext;
class CommandList;
class Resource;
class StructuredBuffer;

class MeshShader
{
public:
    explicit MeshShader(
        const ShaderBlob& meshShader,
        const ShaderBlob& pixelShader,
        const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});

    MeshShader(const MeshShader& other) = delete;
    MeshShader& operator=(const MeshShader& other) = delete;

    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data);

    template<typename T>
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data)
    {
        SetConstantBuffer(commandList, variableName, sizeof(T), &data);
    }

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
    void SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews);
    void SetShaderResource(CommandList& commandList, const std::string& variableName, const Resource& resource);
    void SetStructuredBuffer(CommandList& commandList, const std::string& variableName, const StructuredBuffer& buffer);
    void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
    void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture);
    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView);

    const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }

private:
    friend class CommandContext;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);
    const RootSignature& GetRootSignature() const { return *m_RootSignature; }
    const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
    bool UsesReflectedRootSignature() const { return true; }

    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderReflectionMetadata* outMetadata);
    void BuildPipelineLayout();
    void BuildReflectedRootSignature();

    std::shared_ptr<RootSignature> m_RootSignature;
    ShaderReflectionMetadata m_MeshShaderMetadata;
    ShaderReflectionMetadata m_PixelShaderMetadata;
    std::unique_ptr<PipelineLayout> m_PipelineLayout;
    std::unique_ptr<PipelineBindingSet> m_BindingSet;
    std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
    PipelineDescriptorPool m_DescriptorPool;
    RasterPipelineStateBuilder m_PipelineStateBuilder;
    PipelineStateCache<RasterPipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
};

//Modify End
