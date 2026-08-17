#pragma once

#include <string>
#include <d3d12.h>
#include <wrl.h>
#include <d3d12shader.h>
#include <vector>
#include <memory>
#include <cstdint>

namespace ShaderUtils
{
	Microsoft::WRL::ComPtr<ID3DBlob> LoadShaderFromFile(const std::wstring& fileName);

	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflect(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderSource);
//Modify Begin:2026-07-23 by Hui
	Microsoft::WRL::ComPtr<ID3D12LibraryReflection> ReflectLibrary(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderSource);
//Modify End

	struct ConstantBufferMetadata
	{
		struct Variable
		{
			std::string Name;
			UINT Size;
			UINT Offset;
			std::shared_ptr<uint8_t[]> DefaultValue;
		};

		UINT RegisterIndex;
		UINT Space;
		UINT Size;
		std::string Name;
		std::vector<Variable> Variables;
	};


	std::vector<ConstantBufferMetadata> GetConstantBuffers(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection);
//Modify Begin:2026-07-23 by Hui
	std::vector<ConstantBufferMetadata> GetConstantBuffers(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection);
//Modify End

	struct ShaderResourceViewMetadata
	{
		UINT RegisterIndex;
		UINT Space;
//Modify Begin:2026-07-23 by Hui
		UINT BindCount;
		D3D_SHADER_INPUT_TYPE InputType;
		D3D_SRV_DIMENSION Dimension;
//Modify End
		std::string Name;
	};

	std::vector<ShaderResourceViewMetadata> GetShaderResourceViews(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection);
//Modify Begin:2026-07-23 by Hui
	std::vector<ShaderResourceViewMetadata> GetShaderResourceViews(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection);
//Modify End

//Modify Begin:2026-07-23 by Hui
	struct UnorderedAccessViewMetadata
	{
		UINT RegisterIndex;
		UINT Space;
		UINT BindCount;
		D3D_SHADER_INPUT_TYPE InputType;
		D3D_SRV_DIMENSION Dimension;
		std::string Name;
	};

	std::vector<UnorderedAccessViewMetadata> GetUnorderedAccessViews(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection);
	std::vector<UnorderedAccessViewMetadata> GetUnorderedAccessViews(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection);
//Modify End

//Modify Begin:2026-07-27 by Hui
	struct SamplerMetadata
	{
		UINT RegisterIndex;
		UINT Space;
		UINT BindCount;
		std::string Name;
	};

	std::vector<SamplerMetadata> GetSamplers(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection);
	std::vector<SamplerMetadata> GetSamplers(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection);
//Modify End
}
