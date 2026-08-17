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

//Modify Begin:2026-08-17 by BestHui
	void ExecuteComposite(CommandList& commandList,
		const BloomParameters& parameters,
		const std::shared_ptr<Texture>& sourceColor,
		const std::shared_ptr<Texture>& bloom,
		const RenderTarget& destination);
//Modify End

private:
//Modify Begin:2026-08-17 by BestHui
	std::shared_ptr<Material> m_UpsampleMaterial;
	std::shared_ptr<Material> m_CompositeMaterial;
//Modify End
	std::shared_ptr<Mesh> m_BlitMesh;
};
