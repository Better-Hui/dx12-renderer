#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <memory>
#include <functional>

class CommandQueue;

//Modify Begin:2026-07-28 by BestHui
struct ExternalD3D12Context
{
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* DirectCommandQueue = nullptr;
    ID3D12CommandQueue* ComputeCommandQueue = nullptr;
    ID3D12CommandQueue* CopyCommandQueue = nullptr;
};

class D3D12RenderContext
{
public:
    D3D12RenderContext() = default;

    void InitializeOwned(Microsoft::WRL::ComPtr<ID3D12Device2> device);
    void InitializeExternal(const ExternalD3D12Context& externalContext);

    bool IsValid() const;
    bool UsesExternalDevice() const;

    Microsoft::WRL::ComPtr<ID3D12Device2> GetDevice() const;
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
//Modify Begin:2026-08-07 by BestHui
    void SetFatalErrorHandler(std::function<void(int)> handler);
//Modify End

private:
    void CreateOwnedQueues();
    void WrapExternalQueues(const ExternalD3D12Context& externalContext);
//Modify Begin:2026-08-07 by BestHui
    void ConfigureCommandListDependencies();
//Modify End

    Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
    std::shared_ptr<CommandQueue> m_DirectCommandQueue;
    std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
    std::shared_ptr<CommandQueue> m_CopyCommandQueue;
    bool m_UsesExternalDevice = false;
};
//Modify End
