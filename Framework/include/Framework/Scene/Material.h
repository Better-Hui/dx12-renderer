#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

#include <DX12Library/CommandList.h>

#include <wrl.h>
#include <d3d12.h>

#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

class Material
{
public:
	explicit Material(const std::shared_ptr<Shader>& shader);
    explicit Material(const Material& materialPreset);

    Material& operator=(const Material& other) = delete;

	template<typename T>
	void SetAllVariables(const T& data)
	{
		SetAllVariables(sizeof(data), &data);
	}

	void SetAllVariables(size_t size, const void* data);

	template<typename T> void SetVariable(const std::string& name, const T& data, bool throwOnNotFound = true)
	{
		SetVariable(name, sizeof(data), &data, false, throwOnNotFound);
	}

    template<typename T> void SetArrayVariable(const std::string& name, const std::vector<T>& data, bool throwOnNotFound = true)
	{
	    SetVariable(name, sizeof(T) * data.size(), data.data(), true, throwOnNotFound);
	}

	void SetShaderResourceView(const std::string& name, const ShaderResourceView& shaderResourceView);
	void SetTexture(const std::string& name, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& name, const ShaderResourceView& shaderResourceView);

	void Bind(CommandList& commandList);

	static std::shared_ptr<Material> Create(const std::shared_ptr<Shader>& shader);
	static std::shared_ptr<Material> Create(const Material& materialPreset);

private:

//Modify Begin:2026-08-19 by Hui
	void UploadUniforms(CommandList& commandList);
	//Modify End
    void SetVariable(const std::string& name, size_t size, const void* data, bool array = false, bool throwOnNotFound = true);

	void UploadConstantBuffer(CommandList& commandList);
	void UploadShaderResourceViews(CommandList& commandList);

	std::shared_ptr<Shader> m_Shader;
	const ShaderUtils::ConstantBufferMetadata* m_Metadata;
	std::string m_ConstantBufferName;
//Modify End
	std::unique_ptr<uint8_t[]> m_ConstantBuffer = nullptr;
	size_t m_ConstantBufferSize;

	std::unordered_map <std::string, ShaderResourceView> m_ShaderResourceViews;
};
