#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "CommandQueueFailure.h"

#include <memory>
#include <functional>

class CommandQueue;
class D3D12DeviceContext;
class ResourceStateRegistry;
//Modify Begin:2026-08-21 by Hui
class DiagnosticTelemetrySink;
//Modify End

//Modify Begin:2026-08-07 by Hui
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
    ~D3D12RenderContext();

    void InitializeOwned(Microsoft::WRL::ComPtr<ID3D12Device2> device);
    void InitializeExternal(const ExternalD3D12Context& externalContext);
    void Reset();

    bool IsValid() const;
    bool UsesExternalDevice() const;

    Microsoft::WRL::ComPtr<ID3D12Device2> GetDevice() const;
    std::shared_ptr<D3D12DeviceContext> GetD3D12DeviceContext() const;
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
    std::shared_ptr<ResourceStateRegistry> GetResourceStateRegistry() const;
    void SetFatalErrorHandler(CommandQueueFailureHandler handler);
    void SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) const noexcept;

private:
    void CreateOwnedQueues();
    void WrapExternalQueues(const ExternalD3D12Context& externalContext);
    void CreateDeviceContext();
    Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
    std::shared_ptr<ResourceStateRegistry> m_ResourceStateRegistry;
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
    std::shared_ptr<CommandQueue> m_DirectCommandQueue;
    std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
    std::shared_ptr<CommandQueue> m_CopyCommandQueue;
    bool m_UsesExternalDevice = false;
};
//Modify End
