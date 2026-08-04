#pragma once

#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <memory>
#include <Framework/Scene/Material.h>

class Mesh;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

class BloomUpsample
{
public:
//Modify Begin:2026-07-27 by BestHui
	explicit BloomUpsample(FrameworkDeviceContext& deviceContext, CommandList& commandList);
//Modify End

	void Execute(CommandList& commandList,
		const BloomParameters& parameters,
		const std::shared_ptr<Texture>& source,
		const RenderTarget& destination);

	void Begin(CommandList& commandList);
	void End(CommandList& commandList);

private:
	std::shared_ptr<Material> m_Material;
	std::shared_ptr<Mesh> m_BlitMesh;
};
