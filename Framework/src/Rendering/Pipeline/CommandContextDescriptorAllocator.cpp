//Modify Begin:2026-07-29 by BestHui

#include <Framework/Rendering/Pipeline/CommandContextDescriptorAllocator.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>

void CommandContextDescriptorAllocator::ResetTransientBindings()
{
    m_BoundTables = {};
}

void CommandContextDescriptorAllocator::StageDescriptorTable(
    CommandList& commandList,
    const PipelineBindPoint bindPoint,
    const uint32_t rootParameterIndex,
    const PipelineDescriptorTableAllocation& allocation)
{
    (void)bindPoint;
    Assert(
        rootParameterIndex < MaxRootDescriptorTables,
        "Pipeline descriptor root parameter index exceeds command context cache capacity.");

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = allocation.GetDescriptorHandle();
    BoundTable& boundTable = m_BoundTables[rootParameterIndex];
    if (boundTable.Valid &&
        boundTable.CpuHandle.ptr == cpuHandle.ptr &&
        boundTable.NumHandles == allocation.GetNumHandles() &&
        boundTable.Revision == allocation.GetRevision())
    {
        return;
    }

    commandList.StageDynamicDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        rootParameterIndex,
        0u,
        allocation.GetNumHandles(),
        cpuHandle);

    boundTable.CpuHandle = cpuHandle;
    boundTable.NumHandles = allocation.GetNumHandles();
    boundTable.Revision = allocation.GetRevision();
    boundTable.Valid = true;
}

//Modify End
