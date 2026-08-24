#pragma once

//Modify Begin:2026-08-24 by Hui
#include <Framework/Rendering/PostProcess/BloomParameters.h>
#include <Framework/Scene/Material.h>

#include <memory>

class CommandList;
class FrameworkDeviceContext;
class Mesh;
class RenderTarget;
class Texture;

class BloomUpsample final
{
public:
    BloomUpsample(FrameworkDeviceContext& deviceContext, CommandList& commandList);

    void Execute(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& lowResolutionSource,
        const std::shared_ptr<Texture>& highResolutionSource,
        const RenderTarget& destination);

    void ExecuteComposite(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& sourceColor,
        const std::shared_ptr<Texture>& bloom,
        const RenderTarget& destination);

private:
    void ExecuteInternal(
        CommandList& commandList,
        const BloomParameters& parameters,
        const std::shared_ptr<Texture>& sourceColor,
        const std::shared_ptr<Texture>& bloom,
        const RenderTarget& destination);

    std::shared_ptr<Material> m_Material;
    std::shared_ptr<Mesh> m_BlitMesh;
};
//Modify End
