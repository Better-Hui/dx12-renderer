//Modify Begin:2026-07-27 by BestHui
#include "PipelineDescriptorPool.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <Framework/PipelineLayout.h>

#include <algorithm>

PipelineDescriptorPool::PipelineDescriptorPool(PipelineDescriptorPoolDesc desc)
    : m_Desc(desc)
{
}

void PipelineDescriptorPool::Reset()
{
    m_AllocatedDescriptorSetCount = 0;
    m_AllocatedResourceDescriptorCount = 0;
    m_AllocatedSamplerDescriptorCount = 0;
//Modify Begin:2026-07-29 by BestHui
    m_ResourceDescriptorHeap.Reset();
    m_ResourceCpuStart = {};
    m_ResourceGpuStart = {};
    m_ResourceDescriptorSize = 0;
//Modify End
}

PipelineDescriptorSet PipelineDescriptorPool::AllocateDescriptorSetValue(
    const PipelineLayout& layout,
    const uint32_t variableDescriptorNum)
{
    Assert(m_AllocatedDescriptorSetCount < m_Desc.DescriptorSetMaxNum, "Pipeline descriptor pool ran out of descriptor sets.");

    const uint32_t resourceDescriptorCount = CountResourceDescriptors(layout, variableDescriptorNum);
    const uint32_t samplerDescriptorCount = CountSamplerDescriptors(layout);
    Assert(
        m_AllocatedResourceDescriptorCount + resourceDescriptorCount <= m_Desc.ResourceDescriptorMaxNum,
        "Pipeline descriptor pool ran out of resource descriptors.");
    Assert(
        m_AllocatedSamplerDescriptorCount + samplerDescriptorCount <= m_Desc.SamplerDescriptorMaxNum || samplerDescriptorCount == 0u,
        "Pipeline descriptor pool ran out of sampler descriptors.");

    ++m_AllocatedDescriptorSetCount;
    m_AllocatedSamplerDescriptorCount += samplerDescriptorCount;
//Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorSet descriptorSet(layout);
    auto device = Application::Get().GetDevice();
//Modify Begin:2026-07-29 by BestHui
    if (resourceDescriptorCount > 0u)
    {
        EnsureResourceDescriptorHeap();
    }
//Modify End
    for (const PipelineDescriptorSetDesc& setDesc : layout.GetDescriptorSets())
    {
        for (const PipelineDescriptorRangeDesc& range : setDesc.Ranges)
        {
            if (range.BindingMode != PipelineDescriptorBindingMode::DescriptorTable)
            {
                continue;
            }

//Modify Begin:2026-07-29 by BestHui
            const bool isVariableSizedRange =
                (static_cast<uint32_t>(range.Flags) & static_cast<uint32_t>(PipelineDescriptorRangeFlags::VariableSizedArray)) != 0u;
            const uint32_t descriptorCount = isVariableSizedRange ? variableDescriptorNum : range.DescriptorCount;
            PipelineDescriptorTableAllocation allocation = AllocateResourceDescriptorTable(descriptorCount);
//Modify End
            if (const DescriptorAllocation* defaultDescriptors = layout.FindDefaultDescriptorTable(range.RootParameterIndex))
            {
//Modify Begin:2026-07-29 by BestHui
                const uint32_t defaultDescriptorCount = defaultDescriptors->GetNumHandles();
                const uint32_t copiedDescriptorCount = (std::min)(descriptorCount, defaultDescriptorCount);
                device->CopyDescriptorsSimple(
                    copiedDescriptorCount,
                    allocation.GetDescriptorHandle(),
                    defaultDescriptors->GetDescriptorHandle(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                for (uint32_t descriptorIndex = copiedDescriptorCount; descriptorIndex < descriptorCount; ++descriptorIndex)
                {
                    device->CopyDescriptorsSimple(
                        1u,
                        allocation.GetDescriptorHandle(descriptorIndex),
                        defaultDescriptors->GetDescriptorHandle(0u),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
//Modify End
            }
            descriptorSet.SetDescriptorTableAllocation(range.RootParameterIndex, std::move(allocation));
        }
    }
    return descriptorSet;
//Modify End
}

std::unique_ptr<PipelineDescriptorSet> PipelineDescriptorPool::AllocateDescriptorSet(
    const PipelineLayout& layout,
    const uint32_t variableDescriptorNum)
{
    return std::make_unique<PipelineDescriptorSet>(AllocateDescriptorSetValue(layout, variableDescriptorNum));
}

uint32_t PipelineDescriptorPool::CountResourceDescriptors(
    const PipelineLayout& layout,
    const uint32_t variableDescriptorNum) const
{
    uint32_t descriptorCount = 0;
    for (const PipelineDescriptorSetDesc& setDesc : layout.GetDescriptorSets())
    {
        for (const PipelineDescriptorRangeDesc& range : setDesc.Ranges)
        {
            if (range.BindingMode != PipelineDescriptorBindingMode::DescriptorTable)
            {
                continue;
            }

            const bool isVariableSizedRange =
                (static_cast<uint32_t>(range.Flags) & static_cast<uint32_t>(PipelineDescriptorRangeFlags::VariableSizedArray)) != 0u;
            descriptorCount += isVariableSizedRange ? variableDescriptorNum : range.DescriptorCount;
        }
    }

    return descriptorCount;
}

uint32_t PipelineDescriptorPool::CountSamplerDescriptors(const PipelineLayout& layout) const
{
    (void)layout;
    return 0u;
}

//Modify Begin:2026-07-29 by BestHui
void PipelineDescriptorPool::EnsureResourceDescriptorHeap()
{
    if (m_ResourceDescriptorHeap)
    {
        return;
    }

    auto device = Application::Get().GetDevice();
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = m_Desc.ResourceDescriptorMaxNum;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&m_ResourceDescriptorHeap)));

    m_ResourceCpuStart = m_ResourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_ResourceGpuStart = m_ResourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    m_ResourceDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

PipelineDescriptorTableAllocation PipelineDescriptorPool::AllocateResourceDescriptorTable(const uint32_t descriptorCount)
{
    Assert(descriptorCount > 0u, "Pipeline descriptor table allocation must not be empty.");
    Assert(
        m_AllocatedResourceDescriptorCount + descriptorCount <= m_Desc.ResourceDescriptorMaxNum,
        "Pipeline descriptor pool ran out of resource descriptors.");
    EnsureResourceDescriptorHeap();

    const uint32_t descriptorOffset = m_AllocatedResourceDescriptorCount;
    m_AllocatedResourceDescriptorCount += descriptorCount;

    PipelineDescriptorTableAllocation allocation = {};
    allocation.Heap = m_ResourceDescriptorHeap.Get();
    allocation.CpuDescriptor = m_ResourceCpuStart;
    allocation.CpuDescriptor.ptr += static_cast<SIZE_T>(descriptorOffset) * m_ResourceDescriptorSize;
    allocation.GpuDescriptor = m_ResourceGpuStart;
    allocation.GpuDescriptor.ptr += static_cast<UINT64>(descriptorOffset) * m_ResourceDescriptorSize;
    allocation.NumHandles = descriptorCount;
    allocation.DescriptorSize = m_ResourceDescriptorSize;
    return allocation;
}
//Modify End
//Modify End
