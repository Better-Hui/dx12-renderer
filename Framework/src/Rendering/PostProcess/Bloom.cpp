#include <Framework/Rendering/PostProcess/Bloom.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include "DX12Library/Helpers.h"

#include <algorithm>

namespace
{
	CD3DX12_RESOURCE_DESC CreateRenderTargetDesc(DXGI_FORMAT backBufferFormat, uint32_t width, uint32_t height)
	{
		return CD3DX12_RESOURCE_DESC::Tex2D(backBufferFormat,
			width, height,
			1, 1,
			1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	}
}

//Modify Begin:2026-07-27 by Hui
Bloom::Bloom(FrameworkDeviceContext& deviceContext, CommandList& commandList, uint32_t width, uint32_t height, DXGI_FORMAT backBufferFormat, size_t pyramidSize)
//Modify End
	: m_Width(width)
	, m_Height(height)
//Modify Begin:2026-07-27 by Hui
	, m_Prefilter(deviceContext, commandList)
	, m_Downsample(deviceContext, commandList)
	, m_Upsample(deviceContext, commandList)
//Modify End
{
	// create intermediate textures
	{
		for (size_t i = 0; i < pyramidSize - 1; ++i)
		{
			CreateIntermediateTexture(deviceContext,
				width,
				height,
				m_IntermediateTextures,
				i,
				L"Bloom Intermediate Texture",
				backBufferFormat);
		}
	}
}

void Bloom::Resize(CommandList& commandList, uint32_t width, uint32_t height)
{
	m_Width = width;
	m_Height = height;

	for (size_t i = 0; i < m_IntermediateTextures.size(); ++i)
	{
		uint32_t textureWidth = 0, textureHeight = 0;
		GetIntermediateTextureSize(width, height, i, textureWidth, textureHeight);
		m_IntermediateTextures[i].Resize(commandList, textureWidth, textureHeight);
	}
}

void Bloom::Draw(CommandList& commandList,
	const std::shared_ptr<Texture>& source,
	const RenderTarget& destination,
	const BloomParameters& parameters)
{
	PIXScope(commandList, "Bloom");

	m_Prefilter.Execute(commandList, parameters, source, m_IntermediateTextures[0]);

//Modify Begin:2026-08-17 by Hui
    for (size_t i = 1; i < m_IntermediateTextures.size(); ++i)
    {
        const auto& previousTexture = m_IntermediateTextures[i - 1].GetTexture(Color0);
        auto& currentTexture = m_IntermediateTextures[i];
        m_Downsample.Execute(commandList, parameters, previousTexture, currentTexture);
    }

    for (size_t i = m_IntermediateTextures.size() - 1; i >= 1; --i)
    {
        auto& currentTexture = m_IntermediateTextures[i - 1];
        const auto& nextTexture = m_IntermediateTextures[i].GetTexture(Color0);
        m_Upsample.Execute(commandList, parameters, nextTexture, currentTexture);
    }

    m_Upsample.ExecuteComposite(
        commandList,
        parameters,
        source,
        m_IntermediateTextures[0].GetTexture(Color0),
        destination);
//Modify End
}

void Bloom::GetIntermediateTextureSize(uint32_t width,
	uint32_t height,
	size_t index,
	uint32_t& outWidth,
	uint32_t& outHeight)
{
	outWidth = width;
	outHeight = height;

	for (size_t i = 0; i <= index; i++)
	{
//Modify Begin:2026-08-18 by Hui
		outWidth = std::max(1u, outWidth >> 1u);
		outHeight = std::max(1u, outHeight >> 1u);
//Modify End
	}
}

void Bloom::CreateIntermediateTexture(
	const FrameworkDeviceContext& deviceContext,
	uint32_t width,
	uint32_t height,
	std::vector<RenderTarget>& destinationList,
	size_t index,
	const std::wstring& name,
	DXGI_FORMAT format)
{
	uint32_t textureWidth, textureHeight;
	GetIntermediateTextureSize(width, height, index, textureWidth, textureHeight);

	auto desc = CreateRenderTargetDesc(format, textureWidth, textureHeight);
//Modify Begin:2026-08-12 by Hui
	auto intermediateTexture = std::make_shared<Texture>(
		desc,
		nullptr,
		TextureUsageType::RenderTarget,
		name,
		deviceContext.GetD3D12DeviceContext());
//Modify End

	auto renderTarget = RenderTarget();
	renderTarget.AttachTexture(Color0, intermediateTexture);
	destinationList.push_back(renderTarget);
}
