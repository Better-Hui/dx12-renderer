#pragma once

#include <d3d12.h>

#include <string>
#include <string_view>

//Modify Begin:2026-08-18 by Hui
class D3D12RuntimeLifecycle
{
public:
    virtual ~D3D12RuntimeLifecycle() = default;

    virtual bool InitializeBeforeD3D12(const std::wstring& dataDirectory) = 0;
    virtual bool AttachDevice(ID3D12Device2* device) = 0;
    virtual void Shutdown() = 0;

    [[nodiscard]] virtual std::string_view GetName() const = 0;
    [[nodiscard]] virtual std::string_view GetInitializationStatus() const = 0;
};
//Modify End
