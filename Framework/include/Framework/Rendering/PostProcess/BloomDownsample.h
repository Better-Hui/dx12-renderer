#pragma once

#include <memory>
#include <vector>
#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <DX12Library/Texture.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/CommandList.h>
#include <Framework/Scene/Material.h>

class Mesh;

class BloomDownsample
{
public:
//Modify Begin:2026-07-27 by BestHui
	explicit BloomDownsample(CommandList& commandList);
//Modify End

	void Execute(CommandList& commandList,
		const BloomParameters& parameters,
		const std::shared_ptr<Texture>& source,
		const RenderTarget& destination);

	void Begin(CommandList& commandList);
	void End(CommandList& commandList);

private:
	std::shared_ptr<Mesh> m_BlitMesh;
	std::shared_ptr<Material> m_Material;
};
