#include "DX12LibPCH.h"

#include "D3D12RenderContext.h"

#include "CommandQueue.h"
//Modify Begin:2026-07-30 by Hui
#include "D3D12DeviceContext.h"
#include "ResourceStateRegistry.h"
//Modify End
//Modify Begin:2026-08-07 by Hui
#include "StreamlineRuntime.h"

#include <filesystem>
#include <stdexcept>
//Modify End

//Modify Begin:2026-07-28 by Hui
void D3D12RenderContext::InitializeOwned(
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    const D3D12RenderContextInitializationDesc& initializationDesc)
{
    Assert(device != nullptr, "D3D12 device is null.");
    m_Device = device;
//Modify Begin:2026-07-30 by Hui
    m_ResourceStateRegistry = std::make_shared<ResourceStateRegistry>();
//Modify End
//Modify Begin:2026-08-07 by Hui
    CreateDeviceContext();
//Modify End
    m_UsesExternalDevice = false;
    InitializeStreamlineIfRequested(initializationDesc);
    CreateOwnedQueues();
//Modify Begin:2026-08-07 by Hui
    ConfigureCommandListDependencies();
//Modify End
}

void D3D12RenderContext::InitializeExternal(
    const ExternalD3D12Context& externalContext,
    const D3D12RenderContextInitializationDesc& initializationDesc)
{
    Assert(externalContext.Device != nullptr, "External D3D12 device is required.");
    ThrowIfFailed(externalContext.Device->QueryInterface(IID_PPV_ARGS(&m_Device)));
//Modify Begin:2026-07-30 by Hui
    m_ResourceStateRegistry = std::make_shared<ResourceStateRegistry>();
//Modify End
//Modify Begin:2026-08-07 by Hui
    CreateDeviceContext();
//Modify End
    m_UsesExternalDevice = true;
    InitializeStreamlineIfRequested(initializationDesc);
    WrapExternalQueues(externalContext);
//Modify Begin:2026-08-07 by Hui
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

//Modify Begin:2026-08-07 by Hui
std::shared_ptr<D3D12DeviceContext> D3D12RenderContext::GetD3D12DeviceContext() const
{
    return m_DeviceContext;
}
//Modify End

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
//Modify Begin:2026-07-30 by Hui
        throw std::invalid_argument("Invalid D3D12 command queue type.");
//Modify End
    }
}

std::shared_ptr<StreamlineRuntime> D3D12RenderContext::GetStreamlineRuntime() const
{
    return m_StreamlineRuntime;
}

//Modify Begin:2026-07-30 by Hui
std::shared_ptr<ResourceStateRegistry> D3D12RenderContext::GetResourceStateRegistry() const
{
    return m_ResourceStateRegistry;
}
//Modify End

D3D12RenderContext::~D3D12RenderContext()
{
    m_DirectCommandQueue.reset();
    m_ComputeCommandQueue.reset();
    m_CopyCommandQueue.reset();
//Modify Begin:2026-07-30 by Hui
    m_DeviceContext.reset();
    m_ResourceStateRegistry.reset();
//Modify End
    if (m_StreamlineRuntime != nullptr)
    {
        m_StreamlineRuntime->Shutdown();
    }
}

//Modify Begin:2026-08-07 by Hui
void D3D12RenderContext::CreateDeviceContext()
{
    D3D12DeviceContextDesc deviceContextDesc;
    deviceContextDesc.Device = m_Device;
    deviceContextDesc.ResourceStateRegistry = m_ResourceStateRegistry;
    m_DeviceContext = std::make_shared<D3D12DeviceContext>(std::move(deviceContextDesc));
}
//Modify End

//Modify Begin:2026-08-07 by Hui
void D3D12RenderContext::SetFatalErrorHandler(CommandQueueFailureHandler handler)
{
    m_DirectCommandQueue->SetFatalErrorHandler(handler);
    m_ComputeCommandQueue->SetFatalErrorHandler(handler);
    m_CopyCommandQueue->SetFatalErrorHandler(std::move(handler));
}
//Modify End

void D3D12RenderContext::CreateOwnedQueues()
{
//Modify Begin:2026-08-07 by Hui
    m_DirectCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_DIRECT, m_DeviceContext, m_StreamlineRuntime);
    m_ComputeCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_COMPUTE, m_DeviceContext, m_StreamlineRuntime);
    m_CopyCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_COPY, m_DeviceContext, m_StreamlineRuntime);
//Modify End
}

void D3D12RenderContext::WrapExternalQueues(const ExternalD3D12Context& externalContext)
{
    m_DirectCommandQueue = externalContext.DirectCommandQueue != nullptr
//Modify Begin:2026-08-07 by Hui
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_DeviceContext,
            externalContext.DirectCommandQueue,
            m_StreamlineRuntime)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_DIRECT, m_DeviceContext, m_StreamlineRuntime);
    m_ComputeCommandQueue = externalContext.ComputeCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            m_DeviceContext,
            externalContext.ComputeCommandQueue,
            m_StreamlineRuntime)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COMPUTE, m_DeviceContext, m_StreamlineRuntime);
    m_CopyCommandQueue = externalContext.CopyCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COPY,
            m_DeviceContext,
            externalContext.CopyCommandQueue,
            m_StreamlineRuntime)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COPY, m_DeviceContext, m_StreamlineRuntime);
//Modify End
}

//Modify Begin:2026-08-07 by Hui
void D3D12RenderContext::InitializeStreamlineIfRequested(
    const D3D12RenderContextInitializationDesc& initializationDesc)
{
    if (!initializationDesc.EnableStreamlineInterposer)
    {
        return;
    }

    m_StreamlineRuntime = std::make_shared<StreamlineRuntime>();
    if (m_StreamlineRuntime->Initialize(m_Device.Get(), std::filesystem::current_path().wstring()))
    {
        return;
    }

    const std::string statusMessage = m_StreamlineRuntime->GetStatusMessage();
    m_StreamlineRuntime.reset();
    throw std::runtime_error("Streamline interposer initialization failed: " + statusMessage);
}
//Modify End

//Modify Begin:2026-08-07 by Hui
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
