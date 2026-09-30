#pragma once

#include <memory>
#include <vector>
#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <DX12Library/Texture.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/CommandList.h>
#include <Framework/Scene/Material.h>

class Mesh;
class CommandContext;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End

class BloomPrefilter
{
public:
//Modify Begin:2026-07-27 by Hui
	explicit BloomPrefilter(FrameworkDeviceContext& deviceContext, CommandList& commandList);
//Modify End

//Modify Begin:2026-09-30 by Hui
	void Execute(CommandContext& commandContext, const BloomParameters& parameters, const std::shared_ptr<Texture>& source, const RenderTarget& destination);
//Modify End

private:
	std::shared_ptr<Mesh> m_BlitMesh;
	std::shared_ptr<Material> m_Material;
};
