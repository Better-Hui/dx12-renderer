#pragma once

#include <Framework/Rendering/PostProcess/BloomPrefilter.h>
#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <Framework/Rendering/PostProcess/BloomDownsample.h>
#include <Framework/Rendering/PostProcess/BloomUpsample.h>
#include <memory>
#include <DX12Library/CommandList.h>
#include <cstdint>

class Bloom
{
public:
//Modify Begin:2026-07-27 by Hui
	explicit Bloom(FrameworkDeviceContext& deviceContext, CommandList& commandList, uint32_t width, uint32_t height, DXGI_FORMAT backBufferFormat, size_t pyramidSize = 8);
//Modify End

	void Resize(CommandList& commandList, uint32_t width, uint32_t height);

	void Draw(CommandList& commandList, const std::shared_ptr<Texture>& source, const RenderTarget& destination, const BloomParameters& parameters);

private:
	static void GetIntermediateTextureSize(uint32_t width, uint32_t height, size_t index, uint32_t& outWidth, uint32_t& outHeight);
//Modify Begin:2026-08-12 by Hui
	static void CreateIntermediateTexture(
		const FrameworkDeviceContext& deviceContext,
		uint32_t width,
		uint32_t height,
		std::vector<RenderTarget>& destinationList,
		size_t index,
		const std::wstring& name,
		DXGI_FORMAT format);
//Modify End

	BloomPrefilter m_Prefilter;
	BloomDownsample m_Downsample;
	BloomUpsample m_Upsample;

	uint32_t m_Width, m_Height;
	std::vector<RenderTarget> m_IntermediateTextures;
};
