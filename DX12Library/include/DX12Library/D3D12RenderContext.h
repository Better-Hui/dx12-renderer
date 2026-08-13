#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "CommandQueueFailure.h"

#include <memory>
#include <functional>

class CommandQueue;
class D3D12DeviceContext;
class ResourceStateRegistry;
class StreamlineRuntime;

//Modify Begin:2026-07-28 by BestHui
struct ExternalD3D12Context
{
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* DirectCommandQueue = nullptr;
    ID3D12CommandQueue* ComputeCommandQueue = nullptr;
    ID3D12CommandQueue* CopyCommandQueue = nullptr;
};

//Modify Begin:2026-08-07 by BestHui
struct D3D12RenderContextInitializationDesc
{
    bool EnableStreamlineInterposer = false;
};
//Modify End

class D3D12RenderContext
{
public:
    D3D12RenderContext() = default;
    ~D3D12RenderContext();

    void InitializeOwned(
        Microsoft::WRL::ComPtr<ID3D12Device2> device,
        const D3D12RenderContextInitializationDesc& initializationDesc);
    void InitializeExternal(
        const ExternalD3D12Context& externalContext,
        const D3D12RenderContextInitializationDesc& initializationDesc);

    bool IsValid() const;
    bool UsesExternalDevice() const;

    Microsoft::WRL::ComPtr<ID3D12Device2> GetDevice() const;
//Modify Begin:2026-08-07 by BestHui
    std::shared_ptr<D3D12DeviceContext> GetD3D12DeviceContext() const;
//Modify End
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
    std::shared_ptr<StreamlineRuntime> GetStreamlineRuntime() const;
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<ResourceStateRegistry> GetResourceStateRegistry() const;
//Modify End
//Modify Begin:2026-08-07 by BestHui
    void SetFatalErrorHandler(CommandQueueFailureHandler handler);
//Modify End

private:
    void CreateOwnedQueues();
    void WrapExternalQueues(const ExternalD3D12Context& externalContext);
//Modify Begin:2026-08-07 by BestHui
    void CreateDeviceContext();
//Modify End
//Modify Begin:2026-08-07 by BestHui
    void InitializeStreamlineIfRequested(const D3D12RenderContextInitializationDesc& initializationDesc);
//Modify End
//Modify Begin:2026-08-07 by BestHui
    void ConfigureCommandListDependencies();
//Modify End

    Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<ResourceStateRegistry> m_ResourceStateRegistry;
//Modify End
//Modify Begin:2026-08-07 by BestHui
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
//Modify End
    std::shared_ptr<StreamlineRuntime> m_StreamlineRuntime;
    std::shared_ptr<CommandQueue> m_DirectCommandQueue;
    std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
    std::shared_ptr<CommandQueue> m_CopyCommandQueue;
    bool m_UsesExternalDevice = false;
};
//Modify End
