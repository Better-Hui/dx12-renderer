#pragma once
// ReSharper disable CppRedundantQualifier

#include "RootSignature.h"
#include "DescriptorAllocation.h"
//Modify Begin:2026-08-07 by Hui
#include "DescriptorAllocator.h"
#include "DescriptorRetirementClock.h"
//Modify End

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

//Modify Begin:2026-08-07 by Hui
#include <memory>
//Modify End


constexpr size_t GENERATE_MIPS_CB_ALIGNMENT = 16;

struct alignas(GENERATE_MIPS_CB_ALIGNMENT) GenerateMipsCb
{
	uint32_t SrcMipLevel; // Texture level of source mip
	uint32_t NumMipLevels; // Number of OutMips to write: [1-4]
	uint32_t SrcDimension; // Width and height of the source texture are even or odd.
	uint32_t IsSRgb; // Must apply gamma correction to sRGB textures. Using 32 bit integer for correct alignment
	DirectX::XMFLOAT2 TexelSize; // 1.0 / OutMip1.Dimensions
};

// I don't use scoped enums to avoid the explicit cast that is required to 
// treat these as root indices.
namespace GenerateMips
{
	enum
	{
		GenerateMipsCb,
		SrcMip,
		OutMip,
		NumRootParameters
	};
}

class GenerateMipsPso
{
public:



//Modify Begin:2026-08-07 by Hui
	explicit GenerateMipsPso(Microsoft::WRL::ComPtr<ID3D12Device2> device);
//Modify End

	const RootSignature& GetRootSignature() const
	{
		return m_RootSignature;
	}

	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState()
	{
		return m_PipelineState;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultUav() const
	{
		return m_DefaultUav.GetDescriptorHandle();
	}

	static constexpr size_t MAX_MIP_LEVELS_AT_ONCE = 4;

private:
	RootSignature m_RootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
//Modify Begin:2026-08-07 by Hui
	Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
	std::shared_ptr<DescriptorRetirementClock> m_DescriptorRetirementClock =
		std::make_shared<DescriptorRetirementClock>();
	std::unique_ptr<DescriptorAllocator> m_DescriptorAllocator;
//Modify End

	// Default (no resource) UAV's to pad the unused UAV descriptors.
	// If generating less than 4 mip map levels, the unused mip maps
	// need to be padded with default UAVs (to keep the DX12 runtime happy).
	DescriptorAllocation m_DefaultUav;
};
