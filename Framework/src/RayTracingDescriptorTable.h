#pragma once

//Modify Begin:2026-07-27 by BestHui

#include <DX12Library/DescriptorAllocation.h>
#include <Framework/RayTracingShader.h>

#include <cstdint>
#include <vector>

class CommandContext;
class PipelineDescriptorSet;

class RayTracingDescriptorTable final
{
public:
    void MarkDirty() { m_Dirty = true; }
    bool IsDirty() const { return m_Dirty; }

    void EnsureBuilt(const RayTracingPipelineDesc& desc, const PipelineDescriptorSet& descriptorSet);
    void Stage(const CommandContext& context, const RayTracingPipelineDesc& desc) const;
    void TransitionResources(const CommandContext& context, const RayTracingPipelineDesc& desc, const PipelineDescriptorSet& descriptorSet) const;
    void InsertOutputBarriers(const CommandContext& context, const RayTracingPipelineDesc& desc, const PipelineDescriptorSet& descriptorSet) const;

private:
    DescriptorAllocation m_DescriptorAllocation;
    std::vector<uint32_t> m_DescriptorTableOffsets;
    bool m_Dirty = true;
};

//Modify End
