#pragma once

#include <Framework/ComputeShader.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

class MSAADepthResolvePass
{
public:
//Modify Begin:2026-07-27 by BestHui
    MSAADepthResolvePass();
//Modify End

    void Resolve(CommandList& commandList, const std::shared_ptr<Texture>& source, const std::shared_ptr<Texture>& destination) const;

private:
    ComputeShader m_ComputeShader;
};
