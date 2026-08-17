#pragma once

#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

class MSAADepthResolvePass
{
public:
//Modify Begin:2026-07-27 by Hui
    explicit MSAADepthResolvePass(FrameworkDeviceContext& deviceContext);
//Modify End

    void Resolve(CommandList& commandList, const std::shared_ptr<Texture>& source, const std::shared_ptr<Texture>& destination) const;

private:
    ComputeShader m_ComputeShader;
};
