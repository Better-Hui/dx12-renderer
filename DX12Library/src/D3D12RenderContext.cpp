#include "DX12LibPCH.h"

#include "D3D12RenderContext.h"

#include "CommandQueue.h"

//Modify Begin:2026-07-28 by BestHui
void D3D12RenderContext::InitializeOwned(Microsoft::WRL::ComPtr<ID3D12Device2> device)
{
    Assert(device != nullptr, "D3D12 device is null.");
    m_Device = device;
    m_UsesExternalDevice = false;
    CreateOwnedQueues();
}

void D3D12RenderContext::InitializeExternal(const ExternalD3D12Context& externalContext)
{
    Assert(externalContext.Device != nullptr, "External D3D12 device is required.");
    ThrowIfFailed(externalContext.Device->QueryInterface(IID_PPV_ARGS(&m_Device)));
    m_UsesExternalDevice = true;
    WrapExternalQueues(externalContext);
}

bool D3D12RenderContext::IsValid() const
{
    return m_Device != nullptr && m_DirectCommandQueue != nullptr && m_ComputeCommandQueue != nullptr &&
        m_CopyCommandQueue != nullptr;
}

bool D3D12RenderContext::UsesExternalDevice() const
{
    return m_UsesExternalDevice;
}

Microsoft::WRL::ComPtr<ID3D12Device2> D3D12RenderContext::GetDevice() const
{
    return m_Device;
}

std::shared_ptr<CommandQueue> D3D12RenderContext::GetCommandQueue(const D3D12_COMMAND_LIST_TYPE type) const
{
    switch (type)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return m_DirectCommandQueue;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        return m_ComputeCommandQueue;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        return m_CopyCommandQueue;
    default:
        assert(false && "Invalid command queue type.");
        return nullptr;
    }
}

void D3D12RenderContext::CreateOwnedQueues()
{
    m_DirectCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_ComputeCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    m_CopyCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY);
}

void D3D12RenderContext::WrapExternalQueues(const ExternalD3D12Context& externalContext)
{
    m_DirectCommandQueue = externalContext.DirectCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT, externalContext.DirectCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_ComputeCommandQueue = externalContext.ComputeCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE, externalContext.ComputeCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    m_CopyCommandQueue = externalContext.CopyCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY, externalContext.CopyCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY);
}
//Modify End
