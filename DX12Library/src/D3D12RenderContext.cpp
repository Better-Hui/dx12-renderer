#include "DX12LibPCH.h"

#include "D3D12RenderContext.h"

#include "CommandQueue.h"
//Modify Begin:2026-07-30 by Hui
#include "D3D12DeviceContext.h"
#include "ResourceStateRegistry.h"
//Modify End
#include <stdexcept>

//Modify Begin:2026-08-18 by Hui
void D3D12RenderContext::InitializeOwned(Microsoft::WRL::ComPtr<ID3D12Device2> device)
{
    Assert(device != nullptr, "D3D12 device is null.");
    m_Device = device;
    m_ResourceStateRegistry = std::make_shared<ResourceStateRegistry>();
    CreateDeviceContext();
    m_UsesExternalDevice = false;
    CreateOwnedQueues();
}

void D3D12RenderContext::InitializeExternal(const ExternalD3D12Context& externalContext)
{
    Assert(externalContext.Device != nullptr, "External D3D12 device is required.");
    ThrowIfFailed(externalContext.Device->QueryInterface(IID_PPV_ARGS(&m_Device)));
    m_ResourceStateRegistry = std::make_shared<ResourceStateRegistry>();
    CreateDeviceContext();
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

std::shared_ptr<D3D12DeviceContext> D3D12RenderContext::GetD3D12DeviceContext() const
{
    return m_DeviceContext;
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
        throw std::invalid_argument("Invalid D3D12 command queue type.");
    }
}

std::shared_ptr<ResourceStateRegistry> D3D12RenderContext::GetResourceStateRegistry() const
{
    return m_ResourceStateRegistry;
}

D3D12RenderContext::~D3D12RenderContext()
{
    Reset();
}

void D3D12RenderContext::Reset()
{
    m_DirectCommandQueue.reset();
    m_ComputeCommandQueue.reset();
    m_CopyCommandQueue.reset();
    m_DeviceContext.reset();
    m_ResourceStateRegistry.reset();
    m_Device.Reset();
    m_UsesExternalDevice = false;
}

void D3D12RenderContext::CreateDeviceContext()
{
    D3D12DeviceContextDesc deviceContextDesc;
    deviceContextDesc.Device = m_Device;
    deviceContextDesc.ResourceStateRegistry = m_ResourceStateRegistry;
    m_DeviceContext = std::make_shared<D3D12DeviceContext>(std::move(deviceContextDesc));
}

void D3D12RenderContext::SetFatalErrorHandler(CommandQueueFailureHandler handler)
{
    m_DirectCommandQueue->SetFatalErrorHandler(handler);
    m_ComputeCommandQueue->SetFatalErrorHandler(handler);
    m_CopyCommandQueue->SetFatalErrorHandler(std::move(handler));
}

void D3D12RenderContext::SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) const noexcept
{
    if (m_DeviceContext != nullptr)
    {
        m_DeviceContext->SetDiagnosticTelemetrySink(sink);
    }
    if (m_DirectCommandQueue != nullptr)
    {
        m_DirectCommandQueue->SetDiagnosticTelemetrySink(sink);
    }
    if (m_ComputeCommandQueue != nullptr)
    {
        m_ComputeCommandQueue->SetDiagnosticTelemetrySink(sink);
    }
    if (m_CopyCommandQueue != nullptr)
    {
        m_CopyCommandQueue->SetDiagnosticTelemetrySink(sink);
    }
}

void D3D12RenderContext::CreateOwnedQueues()
{
    m_DirectCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_DIRECT, m_DeviceContext);
    m_ComputeCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_COMPUTE, m_DeviceContext);
    m_CopyCommandQueue = std::make_shared<CommandQueue>(
        D3D12_COMMAND_LIST_TYPE_COPY, m_DeviceContext);
}

void D3D12RenderContext::WrapExternalQueues(const ExternalD3D12Context& externalContext)
{
    m_DirectCommandQueue = externalContext.DirectCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_DeviceContext,
            externalContext.DirectCommandQueue)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_DIRECT, m_DeviceContext);
    m_ComputeCommandQueue = externalContext.ComputeCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            m_DeviceContext,
            externalContext.ComputeCommandQueue)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COMPUTE, m_DeviceContext);
    m_CopyCommandQueue = externalContext.CopyCommandQueue != nullptr
        ? std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COPY,
            m_DeviceContext,
            externalContext.CopyCommandQueue)
        : std::make_shared<CommandQueue>(
            D3D12_COMMAND_LIST_TYPE_COPY, m_DeviceContext);
}

//Modify End
