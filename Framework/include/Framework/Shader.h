#pragma once

#include <d3d12.h>
#include <d3dx12.h>

#include <wrl.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/RenderTargetState.h>
#include "CommonRootSignature.h"

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ShaderResourceView.h"
#include "RasterPipelineStateBuilder.h"
//Modify Begin:2026-07-24 by BestHui
#include "PipelineBindingSet.h"
#include "PipelineDescriptorSet.h"
#include "PipelineLayout.h"
#include "PipelineStateCache.h"
//Modify End
#include "ShaderBlob.h"
#include "ShaderReflection.h"
//Modify Begin:2026-07-23 by BestHui
#include "UnorderedAccessView.h"
//Modify End

class CommandContext;

class Shader
{
public:
	explicit Shader(
		const std::shared_ptr<CommonRootSignature>& rootSignature,
		const ShaderBlob& vertexShaderPath, const ShaderBlob& pixelShaderPath,
//Modify Begin:2026-07-21 by BestHui
		const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {},
		bool collectMetadata = true
//Modify End
	);
//Modify Begin:2026-07-27 by BestHui
	explicit Shader(
		const ShaderBlob& vertexShaderPath,
		const ShaderBlob& pixelShaderPath,
		const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {});
//Modify End

	Shader(const Shader& other) = delete;
	Shader& operator=(const Shader& other) = delete;
	Shader(Shader&& other) = delete;
	Shader& operator=(Shader&& other) = delete;

	void Bind(CommandList& commandList);
	void Unbind(CommandList& commandList);
//Modify Begin:2026-07-27 by BestHui
	void ApplyBindings(CommandList& commandList) const;
//Modify End

	template<typename T>
	void SetPipelineConstantBuffer(CommandList& commandList, const T& data)
	{
		m_CommonRootSignature->SetPipelineConstantBuffer(commandList, data);
	}

	template<typename T>
	void SetModelConstantBuffer(CommandList& commandList, const T& data)
	{
		m_CommonRootSignature->SetMaterialConstantBuffer(commandList, data);
	}

	void SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data);
//Modify Begin:2026-07-24 by BestHui
	bool HasConstantBuffer(const std::string& variableName) const;
	bool HasShaderResourceView(const std::string& variableName) const;
	bool HasUnorderedAccessView(const std::string& variableName) const;
//Modify End
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data);

	template<typename T>
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data)
	{
		SetConstantBuffer(commandList, variableName, sizeof(T), &data);
	}

	void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture);
//Modify Begin:2026-07-23 by BestHui
	void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView);
//Modify End

	using ShaderMetadata = ShaderReflectionMetadata;

	const ShaderMetadata& GetVertexShaderMetadata() const { return m_VertexShaderMetadata; }
	const ShaderMetadata& GetPixelShaderMetadata() const { return m_PixelShaderMetadata; }

private:

//Modify Begin:2026-07-27 by BestHui
	friend class CommandContext;
//Modify End

	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);

	void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);
//Modify Begin:2026-07-24 by BestHui
	void BuildPipelineLayout();
	void BuildReflectedRootSignature();
	const PipelineDescriptorRangeDesc* FindPipelineBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const;
//Modify End

	std::shared_ptr<CommonRootSignature> m_CommonRootSignature;
	std::shared_ptr<RootSignature> m_RootSignature;

	ShaderMetadata m_VertexShaderMetadata;
	ShaderMetadata m_PixelShaderMetadata;
//Modify Begin:2026-07-24 by BestHui
	std::unique_ptr<PipelineLayout> m_PipelineLayout;
	std::unique_ptr<PipelineBindingSet> m_BindingSet;
	std::unique_ptr<PipelineDescriptorSet> m_DescriptorSet;
	bool m_UseReflectedRootSignature = false;
//Modify End

	RasterPipelineStateBuilder m_PipelineStateBuilder;
	PipelineStateCache<RenderTargetState, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
};
