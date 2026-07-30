//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>

#include <cstdint>
#include <array>
#include <memory>

class PipelineLayout;

struct PipelineDescriptorPoolDesc
{
    uint32_t DescriptorSetMaxNum = 1024;
    uint32_t ResourceDescriptorMaxNum = 4096;
    uint32_t SamplerDescriptorMaxNum = 0;
    bool AllowUpdateAfterSet = true;
};

class PipelineDescriptorPool final
{
public:
    explicit PipelineDescriptorPool(PipelineDescriptorPoolDesc desc = {});

    void Reset();
    PipelineDescriptorSet AllocateDescriptorSetValue(
        const PipelineLayout& layout,
        uint32_t variableDescriptorNum = 0,
        uint32_t setIndex = 0);
    std::unique_ptr<PipelineDescriptorSet> AllocateDescriptorSet(
        const PipelineLayout& layout,
        uint32_t variableDescriptorNum = 0,
        uint32_t setIndex = 0);

    const PipelineDescriptorPoolDesc& GetDesc() const { return m_Desc; }
    uint32_t GetAllocatedDescriptorSetCount() const { return m_AllocatedDescriptorSetCount; }
    uint32_t GetAllocatedResourceDescriptorCount() const { return m_AllocatedResourceDescriptorCount; }
    uint32_t GetAllocatedSamplerDescriptorCount() const { return m_AllocatedSamplerDescriptorCount; }
//Modify Begin:2026-07-29 by BestHui
    uint32_t GetAllocatedDescriptorCount(PipelineDescriptorHeapType heapType) const;
//Modify End

private:
    uint32_t CountResourceDescriptors(const PipelineLayout& layout, uint32_t variableDescriptorNum) const;
    uint32_t CountSamplerDescriptors(const PipelineLayout& layout) const;
//Modify Begin:2026-07-29 by BestHui
    PipelineDescriptorTableAllocation AllocateResourceDescriptorTable(uint32_t descriptorCount);
//Modify End

    PipelineDescriptorPoolDesc m_Desc;
    uint32_t m_AllocatedDescriptorSetCount = 0;
    uint32_t m_AllocatedResourceDescriptorCount = 0;
    uint32_t m_AllocatedSamplerDescriptorCount = 0;
//Modify Begin:2026-07-29 by BestHui
    std::array<uint32_t, static_cast<size_t>(PipelineDescriptorHeapType::Count)> m_AllocatedDescriptorCounts = {};
//Modify End
};
//Modify End
