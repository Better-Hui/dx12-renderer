//Modify Begin:2026-08-07 by Hui
#include <Framework/Core/FrameworkDeviceContext.h>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>

#include <utility>

FrameworkDeviceContext::FrameworkDeviceContext(FrameworkDeviceContextDesc desc)
    : m_Desc(std::move(desc))
{
    Assert(m_Desc.DeviceContext != nullptr, "Framework device context requires a D3D12 device context.");
    Assert(m_Desc.DirectQueue != nullptr, "Framework device context requires a direct queue.");
    Assert(m_Desc.ComputeQueue != nullptr, "Framework device context requires a compute queue.");
    Assert(m_Desc.CopyQueue != nullptr, "Framework device context requires a copy queue.");
}

std::shared_ptr<CommandQueue> FrameworkDeviceContext::GetCommandQueue(
    const D3D12_COMMAND_LIST_TYPE type) const
{
    switch (type)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return m_Desc.DirectQueue;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        return m_Desc.ComputeQueue;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        return m_Desc.CopyQueue;
    default:
        throw std::invalid_argument("Unsupported framework command queue type.");
    }
}

void FrameworkDeviceContext::Flush() const
{
    m_Desc.DirectQueue->Flush();
    m_Desc.ComputeQueue->Flush();
    m_Desc.CopyQueue->Flush();
}
//Modify End
