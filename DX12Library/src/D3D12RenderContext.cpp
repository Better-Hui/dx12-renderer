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
//Modify Begin:2026-08-07 by BestHui
    ConfigureCommandListDependencies();
//Modify End
}

void D3D12RenderContext::InitializeExternal(const ExternalD3D12Context& externalContext)
{
    Assert(externalContext.Device != nullptr, "External D3D12 device is required.");
    ThrowIfFailed(externalContext.Device->QueryInterface(IID_PPV_ARGS(&m_Device)));
    m_UsesExternalDevice = true;
    WrapExternalQueues(externalContext);
//Modify Begin:2026-08-07 by BestHui
    ConfigureCommandListDependencies();
//Modify End
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

//Modify Begin:2026-08-07 by BestHui
void D3D12RenderContext::SetFatalErrorHandler(std::function<void(int)> handler)
{
    m_DirectCommandQueue->SetFatalErrorHandler(handler);
    m_ComputeCommandQueue->SetFatalErrorHandler(handler);
    m_CopyCommandQueue->SetFatalErrorHandler(std::move(handler));
}
//Modify End

void D3D12RenderContext::CreateOwnedQueues()
{
//Modify Begin:2026-08-07 by BestHui
    m_DirectCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT, m_Device);
    m_ComputeCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE, m_Device);
    m_CopyCommandQueue = std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY, m_Device);
//Modify End
}

void D3D12RenderContext::WrapExternalQueues(const ExternalD3D12Context& externalContext)
{
    m_DirectCommandQueue = externalContext.DirectCommandQueue != nullptr
//Modify Begin:2026-08-07 by BestHui
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT, m_Device, externalContext.DirectCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_DIRECT, m_Device);
    m_ComputeCommandQueue = externalContext.ComputeCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE, m_Device, externalContext.ComputeCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COMPUTE, m_Device);
    m_CopyCommandQueue = externalContext.CopyCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY, m_Device, externalContext.CopyCommandQueue)
        : std::make_shared<CommandQueue>(D3D12_COMMAND_LIST_TYPE_COPY, m_Device);
//Modify End
}

//Modify Begin:2026-08-07 by BestHui
void D3D12RenderContext::ConfigureCommandListDependencies()
{
    const std::weak_ptr<CommandQueue> computeQueue = m_ComputeCommandQueue;
    const auto requestComputeCommandList = [computeQueue]()
    {
        const std::shared_ptr<CommandQueue> resolvedComputeQueue = computeQueue.lock();
        Assert(resolvedComputeQueue != nullptr, "Compute command queue is unavailable.");
        return resolvedComputeQueue->GetCommandList();
    };

    m_DirectCommandQueue->SetComputeCommandListFactory(requestComputeCommandList);
    m_CopyCommandQueue->SetComputeCommandListFactory(requestComputeCommandList);
    m_DirectCommandQueue->SetComputeCommandQueue(m_ComputeCommandQueue);
    m_CopyCommandQueue->SetComputeCommandQueue(m_ComputeCommandQueue);
}
//Modify End
//Modify End
