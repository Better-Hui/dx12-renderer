#pragma once

#include <d3d12.h>
#include <dxgi.h>

#include <cstdint>
#include <memory>
#include <string>

class Texture;

//Modify Begin:2026-07-23 by BestHui
class RenderTexture final
{
public:
    static std::shared_ptr<Texture> Create2D(
        DXGI_FORMAT format,
        uint32_t width,
        uint32_t height,
        const std::wstring& name,
        D3D12_RESOURCE_FLAGS flags,
        uint16_t arraySize = 1,
        uint16_t mipLevels = 1);

    static std::shared_ptr<Texture> CreateUav2D(
        DXGI_FORMAT format,
        uint32_t width,
        uint32_t height,
        const std::wstring& name,
        uint16_t arraySize = 1,
        uint16_t mipLevels = 1);
};
//Modify End
