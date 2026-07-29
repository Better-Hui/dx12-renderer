//Modify Begin:2026-07-27 by BestHui
#include "PipelineDescriptorPool.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <Framework/PipelineLayout.h>

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
    m_AllocatedDescriptorCounts = {};
//Modify End
}

PipelineDescriptorSet PipelineDescriptorPool::AllocateDescriptorSetValue(
    const PipelineLayout& layout,
    const uint32_t variableDescriptorNum,
    const uint32_t setIndex)
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

    const uint32_t resourceDescriptorOffset = m_AllocatedResourceDescriptorCount;
    const uint32_t samplerDescriptorOffset = m_AllocatedSamplerDescriptorCount;
    ++m_AllocatedDescriptorSetCount;
    m_AllocatedResourceDescriptorCount += resourceDescriptorCount;
    m_AllocatedSamplerDescriptorCount += samplerDescriptorCount;
//Modify Begin:2026-07-29 by BestHui
    m_AllocatedDescriptorCounts[static_cast<size_t>(PipelineDescriptorHeapType::Resource)] = m_AllocatedResourceDescriptorCount;
    m_AllocatedDescriptorCounts[static_cast<size_t>(PipelineDescriptorHeapType::Sampler)] = m_AllocatedSamplerDescriptorCount;
//Modify End
//Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorSet descriptorSet(layout);
//Modify Begin:2026-07-29 by BestHui
    descriptorSet.SetAllocationInfo(
        this,
        setIndex,
        resourceDescriptorOffset,
        samplerDescriptorOffset,
        resourceDescriptorCount,
        samplerDescriptorCount);
//Modify End
    auto device = Application::Get().GetDevice();
    for (const PipelineDescriptorSetDesc& setDesc : layout.GetDescriptorSets())
    {
        for (const PipelineDescriptorRangeDesc& range : setDesc.Ranges)
        {
            if (range.BindingMode != PipelineDescriptorBindingMode::DescriptorTable)
            {
                continue;
            }

            DescriptorAllocation allocation = Application::Get().AllocateDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                range.DescriptorCount);
            if (const DescriptorAllocation* defaultDescriptors = layout.FindDefaultDescriptorTable(range.RootParameterIndex))
            {
                device->CopyDescriptorsSimple(
                    range.DescriptorCount,
                    allocation.GetDescriptorHandle(),
                    defaultDescriptors->GetDescriptorHandle(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            descriptorSet.SetDescriptorTableAllocation(range.RootParameterIndex, std::move(allocation));
        }
    }
    return descriptorSet;
//Modify End
}

std::unique_ptr<PipelineDescriptorSet> PipelineDescriptorPool::AllocateDescriptorSet(
    const PipelineLayout& layout,
    const uint32_t variableDescriptorNum,
    const uint32_t setIndex)
{
    return std::make_unique<PipelineDescriptorSet>(AllocateDescriptorSetValue(layout, variableDescriptorNum, setIndex));
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
uint32_t PipelineDescriptorPool::GetAllocatedDescriptorCount(const PipelineDescriptorHeapType heapType) const
{
    return m_AllocatedDescriptorCounts[static_cast<size_t>(heapType)];
}
//Modify End
//Modify End
