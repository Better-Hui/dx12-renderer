//Modify Begin:2026-09-29 by Hui
#include <Framework/Core/FrameworkDeviceContext.h>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>

#include <utility>

FrameworkDeviceContext::FrameworkDeviceContext(FrameworkDeviceContextDesc desc)
    : m_Desc(std::move(desc))
{
    Assert(m_Desc.DeviceContext != nullptr, "Framework device context requires a D3D12 device context.");
    // Queues are optional for host-owned command recording (for example Unity).
}

std::shared_ptr<CommandQueue> FrameworkDeviceContext::GetCommandQueue(
    const D3D12_COMMAND_LIST_TYPE type) const
{
    switch (type)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        Assert(m_Desc.DirectQueue != nullptr, "No direct submission queue was provided by the host.");
        return m_Desc.DirectQueue;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        Assert(m_Desc.ComputeQueue != nullptr, "No compute submission queue was provided by the host.");
        return m_Desc.ComputeQueue;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        Assert(m_Desc.CopyQueue != nullptr, "No copy submission queue was provided by the host.");
        return m_Desc.CopyQueue;
    default:
        throw std::invalid_argument("Unsupported framework command queue type.");
    }
}

void FrameworkDeviceContext::Flush() const
{
    Assert(m_Desc.DirectQueue || m_Desc.ComputeQueue || m_Desc.CopyQueue,
        "Recording-only contexts cannot wait for host-owned GPU work.");
    if (m_Desc.DirectQueue) m_Desc.DirectQueue->Flush();
    if (m_Desc.ComputeQueue) m_Desc.ComputeQueue->Flush();
    if (m_Desc.CopyQueue) m_Desc.CopyQueue->Flush();
}

bool FrameworkDeviceContext::FlushWithTimeout(const uint32_t timeoutMilliseconds) const
{
    Assert(m_Desc.DirectQueue || m_Desc.ComputeQueue || m_Desc.CopyQueue,
        "Recording-only contexts cannot wait for host-owned GPU work.");
    return (!m_Desc.DirectQueue || m_Desc.DirectQueue->FlushWithTimeout(timeoutMilliseconds)) &&
        (!m_Desc.ComputeQueue || m_Desc.ComputeQueue->FlushWithTimeout(timeoutMilliseconds)) &&
        (!m_Desc.CopyQueue || m_Desc.CopyQueue->FlushWithTimeout(timeoutMilliseconds));
}
//Modify End
