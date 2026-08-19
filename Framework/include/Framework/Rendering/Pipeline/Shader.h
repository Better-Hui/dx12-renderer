#pragma once

#include <d3d12.h>
#include <d3dx12/d3dx12.h>

#include <wrl.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/RenderTargetState.h>

#include <string>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Pipeline/RasterPipelineStateBuilder.h>
//Modify Begin:2026-07-24 by Hui
#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/PipelineStateKey.h>
//Modify End
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
//Modify Begin:2026-07-23 by Hui
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
//Modify End

class CommandContext;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End
//Modify Begin:2026-07-31 by Hui
class IndirectCommandSignature;
//Modify End

class Shader
{
public:
//Modify Begin:2026-07-27 by Hui
	explicit Shader(
		FrameworkDeviceContext& deviceContext,
		const ShaderBlob& vertexShaderPath,
		const ShaderBlob& pixelShaderPath,
		const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
    explicit Shader(
		FrameworkDeviceContext& deviceContext,
		const ShaderBlob& vertexShaderPath,
		const ShaderBlob& pixelShaderPath,
		PipelineLayoutReflectionOptions layoutOptions,
		const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
//Modify End

	Shader(const Shader& other) = delete;
	Shader& operator=(const Shader& other) = delete;
	Shader(Shader&& other) = delete;
	Shader& operator=(Shader&& other) = delete;

	bool HasConstantBuffer(const std::string& variableName) const;
	bool HasShaderResourceView(const std::string& variableName) const;
	bool HasUnorderedAccessView(const std::string& variableName) const;
	using ShaderMetadata = ShaderReflectionMetadata;

	const ShaderMetadata& GetVertexShaderMetadata() const { return m_VertexShaderMetadata; }
	const ShaderMetadata& GetPixelShaderMetadata() const { return m_PixelShaderMetadata; }
//Modify Begin:2026-07-28 by Hui
	const PipelineDescriptorSet& GetDescriptorSet() const { return *m_DescriptorSet; }
//Modify End
//Modify Begin:2026-07-31 by Hui
	std::unique_ptr<IndirectCommandSignature> CreateIndirectDrawCommandSignature(
		const std::string& rootConstantBufferName,
		UINT byteStride) const;
//Modify End

private:
//Modify Begin:2026-07-30 by Hui
	FrameworkDeviceContext& GetDeviceContext() const { return m_DeviceContext; }
//Modify End
//Modify Begin:2026-07-29 by Hui
	friend class CommandContext;
	friend class Material;
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data);

	template<typename T>
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data)
	{
		SetConstantBuffer(commandList, variableName, sizeof(T), &data);
	}

	void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews);
	void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture);
	void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);
	const RootSignature& GetRootSignature() const { return *m_RootSignature; }
	const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
	const PipelineDescriptorPool& GetDescriptorPool() const { return m_DescriptorPool; }
	bool UsesReflectedRootSignature() const { return true; }
	void StageDefaultDescriptorTables(CommandList& commandList) const;
//Modify End

	void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);
//Modify Begin:2026-07-24 by Hui
	void BuildPipelineLayout();
	void BuildReflectedRootSignature();
	const PipelineDescriptorRangeDesc* FindPipelineBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const;
//Modify End

	FrameworkDeviceContext& m_DeviceContext;
	std::shared_ptr<RootSignature> m_RootSignature;

	ShaderMetadata m_VertexShaderMetadata;
	ShaderMetadata m_PixelShaderMetadata;
//Modify Begin:2026-07-27 by Hui
	std::unique_ptr<PipelineLayout> m_PipelineLayout;
	std::unique_ptr<PipelineBindingSet> m_BindingSet;
	std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
	PipelineDescriptorPool m_DescriptorPool;
//Modify End
//Modify Begin:2026-07-31 by Hui
	PipelineLayoutReflectionOptions m_PipelineLayoutOptions;
//Modify End

	RasterPipelineStateBuilder m_PipelineStateBuilder;
//Modify Begin:2026-07-27 by Hui
	PipelineStateCache<RasterPipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
//Modify End
};
