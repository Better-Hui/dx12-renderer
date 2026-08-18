#pragma once

#include <d3d12.h>

#include <memory>

class CommandList;
class D3D12DeviceContext;
class GenerateMipsPso;
class Texture;

//Modify Begin:2026-08-18 by Hui
class MipGenerator final
{
public:
    explicit MipGenerator(std::shared_ptr<D3D12DeviceContext> deviceContext);
    ~MipGenerator();

    MipGenerator(const MipGenerator&) = delete;
    MipGenerator& operator=(const MipGenerator&) = delete;

    void Generate(CommandList& commandList, Texture& texture);

private:
    void GenerateUnorderedAccessMips(CommandList& commandList, Texture& texture, DXGI_FORMAT format);

    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
    std::unique_ptr<GenerateMipsPso> m_Pipeline;
};
//Modify End
