//Modify Begin:2026-10-04 by Hui

#include <Framework/Rendering/Pipeline/CommandContextDescriptorAllocator.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
//Modify Begin:2026-10-02 by Hui
#include <DX12Library/PerformanceScope.h>
//Modify End
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>

void CommandContextDescriptorAllocator::ResetTransientBindings()
{
    m_BoundTables = {};
}

//Modify Begin:2026-10-02 by Hui
void CommandContextDescriptorAllocator::ResetTransientBindings(const PipelineBindPoint bindPoint)
{
    if (bindPoint == PipelineBindPoint::Graphics)
    {
        m_BoundTables[0] = {};
        return;
    }
    m_BoundTables[1] = {};
    m_BoundTables[2] = {};
}
//Modify End

void CommandContextDescriptorAllocator::SetBindlessDescriptorHeap(BindlessDescriptorHeap* bindlessDescriptorHeap)
{
    if (m_BindlessDescriptorHeap == bindlessDescriptorHeap)
    {
        return;
    }
    m_BindlessDescriptorHeap = bindlessDescriptorHeap;
    ResetTransientBindings();
}

void CommandContextDescriptorAllocator::StageDescriptorTable(
    CommandList& commandList,
    const PipelineBindPoint bindPoint,
    const uint32_t rootParameterIndex,
    const PipelineDescriptorTableAllocation& allocation)
{
    Assert(
        rootParameterIndex < MaxRootDescriptorTables,
        "Pipeline descriptor root parameter index exceeds command context cache capacity.");

    const uint32_t bindPointIndex = bindPoint == PipelineBindPoint::Graphics ? 0u :
        bindPoint == PipelineBindPoint::Compute ? 1u : 2u;

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = allocation.GetDescriptorHandle();
    BoundTable& boundTable = m_BoundTables[bindPointIndex][rootParameterIndex];
    //Modify Begin:2026-10-02 by Hui
    // A command-context cache hit must avoid the bindless page lock and table lookup.
    if (boundTable.Valid &&
        boundTable.CpuHandle.ptr == cpuHandle.ptr &&
        boundTable.NumHandles == allocation.GetNumHandles() &&
        boundTable.Revision == allocation.GetRevision())
    {
        return;
    }
    //Modify End
    if (m_BindlessDescriptorHeap != nullptr)
    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_RECORDING_SCOPE("descriptor_table.bindless");
        //Modify End
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
            m_BindlessDescriptorHeap->GetOrCreateDescriptorTable(allocation);

        if (bindPoint == PipelineBindPoint::Graphics)
        {
            commandList.SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
        }
        else
        {
            commandList.SetComputeRootDescriptorTable(rootParameterIndex, gpuHandle);
        }

        boundTable.CpuHandle = cpuHandle;
        boundTable.NumHandles = allocation.GetNumHandles();
        boundTable.Revision = allocation.GetRevision();
        boundTable.Valid = true;
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
