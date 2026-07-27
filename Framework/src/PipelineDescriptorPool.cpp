//Modify Begin:2026-07-27 by BestHui
#include "PipelineDescriptorPool.h"

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
    m_AllocatedResourceDescriptorCount += resourceDescriptorCount;
    m_AllocatedSamplerDescriptorCount += samplerDescriptorCount;
    return PipelineDescriptorSet(layout);
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
//Modify End
