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
#include "FrameworkDeprecated.h"
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
#include "UnorderedAccessView.h"
//Modify End

class CommandContext;

class Shader
{
public:
//Modify Begin:2026-07-27 by BestHui
	FRAMEWORK_DEPRECATED("Use Shader(const ShaderBlob&, const ShaderBlob&, ...) and reflected named bindings.")
//Modify End
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
//Modify Begin:2026-07-27 by BestHui
	FRAMEWORK_DEPRECATED("Use SetConstantBuffer(CommandList&, const std::string&, ...) with reflected named bindings.")
//Modify End
	void SetPipelineConstantBuffer(CommandList& commandList, const T& data)
	{
		m_CommonRootSignature->SetPipelineConstantBuffer(commandList, data);
	}

	template<typename T>
//Modify Begin:2026-07-27 by BestHui
	FRAMEWORK_DEPRECATED("Use SetConstantBuffer(CommandList&, const std::string&, ...) with reflected named bindings.")
//Modify End
	void SetModelConstantBuffer(CommandList& commandList, const T& data)
	{
		m_CommonRootSignature->SetMaterialConstantBuffer(commandList, data);
	}

//Modify Begin:2026-07-27 by BestHui
	FRAMEWORK_DEPRECATED("Use SetConstantBuffer(CommandList&, const std::string&, ...) with reflected named bindings.")
//Modify End
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
//Modify Begin:2026-07-27 by BestHui
	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);
	const RootSignature& GetRootSignature() const { return *m_RootSignature; }
	const PipelineLayout* GetPipelineLayout() const { return m_PipelineLayout.get(); }
	bool UsesReflectedRootSignature() const { return m_UseReflectedRootSignature; }
	void StageDefaultDescriptorTables(CommandList& commandList) const;
//Modify End

private:

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
//Modify Begin:2026-07-27 by BestHui
	PipelineDescriptorPool m_DescriptorPool;
//Modify End
	bool m_UseReflectedRootSignature = false;
//Modify End

	RasterPipelineStateBuilder m_PipelineStateBuilder;
//Modify Begin:2026-07-27 by BestHui
	PipelineStateCache<RasterPipelineStateKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
//Modify End
};
